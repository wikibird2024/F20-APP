#include "f20/protocol.h"

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
    if (!j || !j->contains("id") || !(*j)["id"].is_number_integer() ||
        !j->contains("cmd") || !(*j)["cmd"].is_string())
        return std::nullopt;

    Request request;
    request.id = (*j)["id"].get<int>();
    request.cmd = (*j)["cmd"].get<std::string>();
    if (j->contains("params") && (*j)["params"].is_object())
        request.params = (*j)["params"];
    return request;
}

Incoming parseIncoming(const std::string& line) {
    const auto j = tryParse(line);
    if (!j)
        return std::monostate{};

    if (j->contains("event") && (*j)["event"].is_string()) {
        Event event;
        event.event = (*j)["event"].get<std::string>();
        if (j->contains("data") && (*j)["data"].is_object())
            event.data = (*j)["data"];
        return event;
    }

    if (j->contains("id") && (*j)["id"].is_number_integer() &&
        j->contains("ok") && (*j)["ok"].is_boolean()) {
        Reply reply;
        reply.id = (*j)["id"].get<int>();
        reply.ok = (*j)["ok"].get<bool>();
        if (reply.ok) {
            if (j->contains("result") && (*j)["result"].is_object())
                reply.result = (*j)["result"];
        } else if (j->contains("error") && (*j)["error"].is_object()) {
            const json& e = (*j)["error"];
            reply.errorCode = e.value("code", "");
            reply.errorMessage = e.value("message", "");
        }
        return reply;
    }

    return std::monostate{};
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
