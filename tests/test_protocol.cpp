#include "doctest.h"
#include "f20/protocol.h"

using namespace f20;

TEST_CASE("request round-trip") {
    Request request;
    request.id = 7;
    request.cmd = "measure";
    request.params = {{"addToHistory", false}};

    const auto parsed = parseRequest(serialize(request));
    REQUIRE(parsed.has_value());
    CHECK(parsed->id == 7);
    CHECK(parsed->cmd == "measure");
    CHECK(parsed->params["addToHistory"] == false);
}

TEST_CASE("ok reply round-trip through parseIncoming") {
    const Reply reply = okReply(3, {{"gof", 0.987}});
    const Incoming incoming = parseIncoming(serialize(reply));
    REQUIRE(std::holds_alternative<Reply>(incoming));
    const auto& r = std::get<Reply>(incoming);
    CHECK(r.id == 3);
    CHECK(r.ok);
    CHECK(r.result["gof"] == 0.987);
}

TEST_CASE("error reply carries code and message") {
    const Reply reply = errorReply(9, "busy", "command in progress");
    const Incoming incoming = parseIncoming(serialize(reply));
    REQUIRE(std::holds_alternative<Reply>(incoming));
    const auto& r = std::get<Reply>(incoming);
    CHECK_FALSE(r.ok);
    CHECK(r.errorCode == "busy");
    CHECK(r.errorMessage == "command in progress");
}

TEST_CASE("events are recognized and carry data") {
    Event event;
    event.event = "filmeasureDied";
    event.data = {{"exitCode", -1}};
    const Incoming incoming = parseIncoming(serialize(event));
    REQUIRE(std::holds_alternative<Event>(incoming));
    CHECK(std::get<Event>(incoming).data["exitCode"] == -1);
}

TEST_CASE("garbage input never throws, returns empty") {
    CHECK_FALSE(parseRequest("not json").has_value());
    CHECK_FALSE(parseRequest("{\"cmd\":\"x\"}").has_value()); // id missing
    CHECK(std::holds_alternative<std::monostate>(parseIncoming("{{{{")));
    CHECK(std::holds_alternative<std::monostate>(parseIncoming("[1,2]")));
}
