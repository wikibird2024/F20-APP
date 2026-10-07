#include "socketserver.h"

#include "f20/framing.h"
#include "f20/protocol.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <thread>

namespace f20sim
{
namespace
{

// Writes all of `data`: send() may take only part of it, or be interrupted
// by a signal (libuv's uv__write loops the same way, src/unix/stream.c).
// MSG_NOSIGNAL: a client that hung up gives EPIPE here instead of a SIGPIPE
// that would kill the process.
bool writeAll(int fd, const std::string &data)
{
    std::size_t sent = 0;
    while (sent < data.size()) {
        const ssize_t n = ::send(fd, data.data() + sent, data.size() - sent, MSG_NOSIGNAL);
        if (n < 0) {
            if (errno == EINTR)
                continue;
            return false;
        }
        sent += static_cast<std::size_t>(n);
    }
    return true;
}

// One request line -> one reply. The exception boundary of the server: one
// bad request must never stop it (clangd's JSONTransport turns handler
// errors into an error reply the same way). The phase-6 bridge needs the
// same boundary around its FIRemote calls.
f20::Reply answer(SimEngine &engine, const std::string &line)
{
    const auto request = f20::parseRequest(line);
    if (!request)
        return f20::errorReply(0, "filmeasureError", "malformed request line");
    try {
        return engine.handle(*request);
    } catch (const std::exception &e) {
        return f20::errorReply(request->id, "filmeasureError", std::string("internal error: ") + e.what());
    }
}

// Serves one client until it closes, errors, overflows a line, or the
// engine sees "quit".
void serveClient(int client, SimEngine &engine)
{
    f20::LineSplitter splitter;
    char              buffer[4096];
    for (;;) {
        const ssize_t n = ::read(client, buffer, sizeof buffer);
        if (n < 0 && errno == EINTR)
            continue;
        if (n <= 0)
            return; // client closed or error -> wait for the next client
        for (const std::string &line : splitter.feed(buffer, static_cast<std::size_t>(n))) {
            if (line.empty())
                continue;
            if (!writeAll(client, f20::serialize(answer(engine, line)) + "\n"))
                return;
        }
        if (splitter.overflowed()) {
            const f20::Reply reply = f20::errorReply(0, "filmeasureError", "request line too long");
            writeAll(client, f20::serialize(reply) + "\n");
            std::printf("client sent a line over %zu bytes - closing it\n", f20::LineSplitter::kDefaultMaxLineBytes);
            return;
        }
        if (engine.quitRequested())
            return;
    }
}

} // namespace

SocketServer::SocketServer(std::uint16_t port) : port_(port)
{
}

int SocketServer::run(SimEngine &engine)
{
    const int listenFd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (listenFd < 0) {
        std::perror("socket");
        return 1;
    }
    const int on = 1;
    ::setsockopt(listenFd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof on);

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK); // localhost only (spec §4)
    addr.sin_port = htons(port_);
    if (::bind(listenFd, reinterpret_cast<sockaddr *>(&addr), sizeof addr) < 0) {
        std::perror("bind");
        ::close(listenFd);
        return 1;
    }
    if (::listen(listenFd, 1) < 0) {
        std::perror("listen");
        ::close(listenFd);
        return 1;
    }
    std::printf("f20bridge-sim listening on 127.0.0.1:%u\n", port_);

    while (!engine.quitRequested()) {
        const int client = ::accept(listenFd, nullptr, nullptr);
        if (client < 0) {
            if (errno == EINTR || errno == ECONNABORTED)
                continue;
            // e.g. EMFILE (out of file descriptors): retrying at once would
            // spin at 100 % CPU, so wait a moment first.
            std::perror("accept");
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }
        std::printf("client connected\n");
        serveClient(client, engine);
        ::close(client);
        std::printf("client disconnected\n");
    }

    ::close(listenFd);
    return 0;
}

} // namespace f20sim
