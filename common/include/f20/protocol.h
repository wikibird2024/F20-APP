#pragma once
#include <nlohmann/json.hpp>

#include <optional>
#include <string>
#include <variant>

namespace f20 {

using json = nlohmann::json;

// One request, app -> bridge (spec §5.3):
//   {"id": 7, "cmd": "measure", "params": {"addToHistory": false}}
struct Request {
    int id = 0;
    std::string cmd;
    json params = json::object();
};

// One reply, bridge -> app, matched to its request by id:
//   {"id": 7, "ok": true, "result": {...}}
//   {"id": 7, "ok": false, "error": {"code": "busy", "message": "..."}}
struct Reply {
    int id = 0;
    bool ok = true;
    json result = json::object();
    std::string errorCode;    // §5.2 code, set when ok == false
    std::string errorMessage; // original exception text / detail
};

// Unsolicited event, bridge -> app (no id):
//   {"event": "filmeasureDied", "data": {"exitCode": -1}}
struct Event {
    std::string event;
    json data = json::object();
};

// Serialization: one line of JSON, WITHOUT the trailing '\n'
// (the socket layer appends it).
std::string serialize(const Request& request);
std::string serialize(const Reply& reply);
std::string serialize(const Event& event);

// Parsing. Invalid JSON or a missing required field returns nullopt /
// monostate - never throws, because socket input is untrusted.
std::optional<Request> parseRequest(const std::string& line);

// The app side receives replies AND events on the same socket.
using Incoming = std::variant<std::monostate, Reply, Event>;
Incoming parseIncoming(const std::string& line);

// Convenience builders for the sim/bridge side.
Reply okReply(int id, json result = json::object());
Reply errorReply(int id, const std::string& code, const std::string& message);

} // namespace f20
