#pragma once
#include "simengine.h"

#include <cstdint>

namespace f20sim {

// Blocking TCP server on 127.0.0.1:<port>, one client at a time
// (spec §5.1 lifecycle). POSIX sockets; the Windows bridge will reuse the
// same shape behind a winsock #ifdef in phase 6.
class SocketServer {
public:
    explicit SocketServer(std::uint16_t port);

    // Serves until the engine sees "quit". Returns 0 on clean exit.
    int run(SimEngine& engine);

private:
    std::uint16_t port_;
};

} // namespace f20sim
