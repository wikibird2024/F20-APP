#include "simengine.h"
#include "socketserver.h"

#include <cstdio>
#include <cstdlib>

int main(int argc, char** argv) {
    std::printf("f20bridge-sim 0.1.0 - F20 bridge simulator\n");

    std::uint16_t port = 5555;
    if (argc > 1)
        port = static_cast<std::uint16_t>(std::atoi(argv[1]));

    f20sim::SimEngine engine;
    f20sim::SocketServer server(port);
    return server.run(engine);
}
