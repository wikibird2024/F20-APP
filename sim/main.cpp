#include "simengine.h"
#include "socketserver.h"

#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstdlib>

int main(int argc, char** argv) {
    std::printf("f20bridge-sim 0.1.0 - F20 bridge simulator\n");

    // A client that hangs up mid-reply must give a write error, not a
    // SIGPIPE that ends the process (Redis does the same, server.c).
    std::signal(SIGPIPE, SIG_IGN);

    std::uint16_t port = 5555;
    if (argc > 1) {
        char* end = nullptr;
        errno = 0;
        const long value = std::strtol(argv[1], &end, 10);
        if (end == argv[1] || *end != '\0' || errno != 0 || value < 1 || value > 65535) {
            std::fprintf(stderr, "usage: f20bridge-sim [port 1-65535]\n");
            return 2;
        }
        port = static_cast<std::uint16_t>(value);
    }

    f20sim::SimEngine engine;
    f20sim::SocketServer server(port);
    return server.run(engine);
}
