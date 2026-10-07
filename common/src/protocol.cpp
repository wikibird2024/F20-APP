#include "f20/protocol.h"

#include "f20/jsonread.h"

namespace f20 {
namespace {

std::optional<json> tryParse(const std::string& line) {
    json j = json::parse(line, nullptr, /*allow_exceptions=*/false);
    if (j.is_discarded() || !j.is_object())
        return std::nullopt;
    return j;
}

} // namespace

std::string serialize(const Request& request) {
    json j{{"id", request.id}, {"cmd", request.cmd}};
    if (!request.params.empty())
        j["params"] = request.params;
    return j.dump();
}

std::string serialize(const Reply& reply) {
    json j{{"id", reply.id}, {"ok", reply.ok}};
    if (reply.ok) {
        j["result"] = reply.result;
    } else {
        j["error"] = {{"code", reply.errorCode}, {"message", reply.errorMessage}};
    }
    return j.dump();
}

std::string serialize(const Event& event) {
    return json{{"event", event.event}, {"data", event.data}}.dump();
}

std::optional<Request> parseRequest(const std::string& line) {
    const auto j = tryParse(line);
    if (!j)
        return std::nullopt;
    const auto id = intAt(*j, "id");
    auto cmd = stringAt(*j, "cmd");
    if (!id || !cmd)
        return std::nullopt;

    Request request;
    request.id = *id;
    request.cmd = std::move(*cmd);
    if (const auto params = j->find("params"); params != j->end() && params->is_object())
        request.params = *params;
    return request;
}

Incoming parseIncoming(const std::string& line) {
    const auto j = tryParse(line);
    if (!j)
        return std::monostate{};

    if (auto name = stringAt(*j, "event")) {
        Event event;
        event.event = std::move(*name);
        if (const auto data = j->find("data"); data != j->end() && data->is_object())
            event.data = *data;
        return event;
    }

    const auto id = intAt(*j, "id");
    const auto ok = boolAt(*j, "ok");
    if (!id || !ok)
        return std::monostate{};

    Reply reply;
    reply.id = *id;
    reply.ok = *ok;
    if (reply.ok) {
        if (const auto result = j->find("result"); result != j->end() && result->is_object())
            reply.result = *result;
    } else if (const auto error = j->find("error"); error != j->end()) {
        // A code or message of the wrong type is read as empty, not thrown:
        // the reply is still an error and still answers its request.
        reply.errorCode = stringAt(*error, "code").value_or("");
        reply.errorMessage = stringAt(*error, "message").value_or("");
    }
    return reply;
}

Reply okReply(int id, json result) {
    Reply reply;
    reply.id = id;
    reply.ok = true;
    reply.result = std::move(result);
    return reply;
}

Reply errorReply(int id, const std::string& code, const std::string& message) {
    Reply reply;
    reply.id = id;
    reply.ok = false;
    reply.errorCode = code;
    reply.errorMessage = message;
    return reply;
}

} // namespace f20
