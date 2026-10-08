#include "simengine.h"
#include "socketserver.h"

#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>

int main(int argc, char** argv) {
    std::printf("f20bridge-sim 0.1.0 - F20 bridge simulator\n");

    // A client that hangs up mid-reply must give a write error, not a
    // SIGPIPE that ends the process (Redis does the same, server.c).
    std::signal(SIGPIPE, SIG_IGN);

    std::uint16_t port = 5555;
    f20sim::SimConfig config;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--stored-baseline") == 0) {
            config.storedBaselineOnDisk = true;
            continue;
        }
        if (std::strcmp(argv[i], "--startup-warning") == 0) {
            config.startupWarning = true;
            continue;
        }
        if (std::strcmp(argv[i], "--filmeasure-dies-after") == 0 && i + 1 < argc) {
            config.filmeasureDiesAfter = std::atoi(argv[++i]);
            continue;
        }
        char* end = nullptr;
        errno = 0;
        const long value = std::strtol(argv[i], &end, 10);
        if (end == argv[i] || *end != '\0' || errno != 0 || value < 1 || value > 65535) {
            std::fprintf(stderr, "usage: f20bridge-sim [port 1-65535] [--stored-baseline] [--startup-warning]\n"
                                 "                     [--filmeasure-dies-after N]\n");
            return 2;
        }
        port = static_cast<std::uint16_t>(value);
    }

    f20sim::SimEngine engine(config);
    f20sim::SocketServer server(port);
    return server.run(engine);
}
