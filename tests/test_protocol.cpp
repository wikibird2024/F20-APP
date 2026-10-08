#include "doctest.h"
#include "f20/logtext.h"
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

TEST_CASE("fields of the wrong type never throw (nlohmann value() would)") {
    // error.code is a number, error.message an array: still an error reply.
    const Incoming incoming = parseIncoming(R"({"id":1,"ok":false,"error":{"code":5,"message":[1]}})");
    REQUIRE(std::holds_alternative<Reply>(incoming));
    const auto& reply = std::get<Reply>(incoming);
    CHECK_FALSE(reply.ok);
    CHECK(reply.errorCode.empty());
    CHECK(reply.errorMessage.empty());

    CHECK(std::holds_alternative<std::monostate>(parseIncoming(R"({"id":"7","ok":true})")));
    CHECK(std::holds_alternative<std::monostate>(parseIncoming(R"({"id":1,"ok":"yes"})")));
    CHECK(std::holds_alternative<std::monostate>(parseIncoming(R"({"event":5})")));
    CHECK_FALSE(parseRequest(R"({"id":1,"cmd":7})").has_value());
}

TEST_CASE("an id that does not fit in int is rejected, not narrowed") {
    CHECK_FALSE(parseRequest(R"({"id":4294967297,"cmd":"getStatus"})").has_value());
    CHECK(std::holds_alternative<std::monostate>(parseIncoming(R"({"id":-4294967297,"ok":true})")));
}

TEST_CASE("log text: a spectrum reply keeps its id but not its 512 values") {
    std::vector<double> values(512, 0.25);
    const std::string line =
        serialize(okReply(7, {{"wavelengthNm", values}, {"reflectance", values}}));
    REQUIRE(line.size() > 1000);

    const std::string logged = shortenForLog(line);
    CHECK(logged.find(R"("id":7)") != std::string::npos);
    CHECK(logged.find(R"("reflectance":"[512 values]")") != std::string::npos);
    CHECK(logged.size() < 200);
}

TEST_CASE("log text: short lines are logged exactly as sent") {
    const std::string line = R"({"id":3,"cmd":"setRecipe","params":{"name":"SiO2 on Si"}})";
    CHECK(shortenForLog(line) == line);
}

TEST_CASE("log text: keys keep their wire order; nested long arrays are collapsed") {
    std::vector<int> many(100, 1);
    const std::string line = nlohmann::json{{"id", 1}, {"result", {{"layers", {{{"pts", many}}}}}}}.dump();
    const std::string logged = shortenForLog(line, 16, 50);
    CHECK(logged.find("[100 values]") != std::string::npos);
    CHECK(logged.find("\"id\"") < logged.find("\"result\""));
}

TEST_CASE("log text: long non-JSON is cut with its length, garbage never throws") {
    const std::string text(5000, 'x');
    const std::string logged = shortenForLog(text);
    CHECK(logged.size() < 1100);
    CHECK(logged.find("... (5000 chars)") != std::string::npos);

    // Cut inside a 2-byte UTF-8 character: the character is dropped whole.
    const std::string accents = std::string(999, 'a') + "\xc3\xa9" + std::string(10, 'b');
    CHECK(shortenForLog(accents).substr(0, 1000) == std::string(999, 'a') + ".");

    CHECK_NOTHROW(shortenForLog(std::string(2000, '{')));
    CHECK_NOTHROW(shortenForLog("[" + std::string(2000, '\xff') + "]"));
}
