#include "doctest.h"
#include "f20/framing.h"

using f20::LineSplitter;

TEST_CASE("complete lines come out, tail stays buffered") {
    LineSplitter splitter;
    auto lines = splitter.feed("{\"a\":1}\n{\"b\":2}\n{\"c\":");
    REQUIRE(lines.size() == 2);
    CHECK(lines[0] == "{\"a\":1}");
    CHECK(lines[1] == "{\"b\":2}");

    lines = splitter.feed("3}\n");
    REQUIRE(lines.size() == 1);
    CHECK(lines[0] == "{\"c\":3}");
}

TEST_CASE("a line split across many reads is reassembled") {
    LineSplitter splitter;
    CHECK(splitter.feed("{\"id\"").empty());
    CHECK(splitter.feed(":7").empty());
    auto lines = splitter.feed("}\n");
    REQUIRE(lines.size() == 1);
    CHECK(lines[0] == "{\"id\":7}");
}

TEST_CASE("CRLF line endings are tolerated") {
    LineSplitter splitter;
    auto lines = splitter.feed("abc\r\ndef\n");
    REQUIRE(lines.size() == 2);
    CHECK(lines[0] == "abc");
    CHECK(lines[1] == "def");
}
