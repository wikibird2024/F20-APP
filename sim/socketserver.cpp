#include "socketserver.h"

#include "f20/framing.h"
#include "f20/protocol.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstdio>
#include <cstring>

namespace f20sim {

SocketServer::SocketServer(std::uint16_t port) : port_(port) {}

int SocketServer::run(SimEngine& engine) {
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
    if (::bind(listenFd, reinterpret_cast<sockaddr*>(&addr), sizeof addr) < 0) {
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
        if (client < 0)
            continue;
        std::printf("client connected\n");

        f20::LineSplitter splitter;
        char buffer[4096];
        for (;;) {
            const ssize_t n = ::read(client, buffer, sizeof buffer);
            if (n <= 0)
                break; // client closed or error -> wait for the next client
            for (const std::string& line : splitter.feed(buffer, static_cast<std::size_t>(n))) {
                if (line.empty())
                    continue;
                f20::Reply reply;
                if (const auto request = f20::parseRequest(line)) {
                    reply = engine.handle(*request);
                } else {
                    reply = f20::errorReply(0, "filmeasureError",
                                            "malformed request line");
                }
                const std::string out = f20::serialize(reply) + "\n";
                if (::write(client, out.data(), out.size()) < 0)
                    break;
            }
            if (engine.quitRequested())
                break;
        }
        ::close(client);
        std::printf("client disconnected\n");
    }

    ::close(listenFd);
    return 0;
}

} // namespace f20sim
