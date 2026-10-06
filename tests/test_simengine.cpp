#include "doctest.h"
#include "simengine.h"

using f20::Request;
using f20sim::SimEngine;

namespace {

Request req(int id, const char* cmd, f20::json params = f20::json::object()) {
    Request r;
    r.id = id;
    r.cmd = cmd;
    r.params = std::move(params);
    return r;
}

// Runs the full happy-path baseline (spec §7.2 order).
void runBaseline(SimEngine& engine) {
    CHECK(engine.handle(req(1, "baselineSetRefMat", {{"name", "Si"}})).ok);
    CHECK(engine.handle(req(2, "baselineStep1")).ok);
    CHECK(engine.handle(req(3, "baselineStep2")).ok);
    CHECK(engine.handle(req(4, "baselineStep3")).ok);
    CHECK(engine.handle(req(5, "baselineCommit")).ok);
}

} // namespace

TEST_CASE("measure before baseline is measureNotReady (spec test 4)") {
    SimEngine engine;
    engine.handle(req(1, "setRecipe", {{"name", "SiO2 on Si"}}));
    const auto reply = engine.handle(req(2, "measure"));
    CHECK_FALSE(reply.ok);
    CHECK(reply.errorCode == "measureNotReady");
}

TEST_CASE("baseline steps out of order are rejected like FILMeasure") {
    SimEngine engine;
    engine.handle(req(1, "baselineSetRefMat", {{"name", "Si"}}));

    // step 2 without step 1
    auto reply = engine.handle(req(2, "baselineStep2"));
    CHECK_FALSE(reply.ok);
    CHECK(reply.errorCode == "baselineOrderWrong");

    // step 3 without step 2
    engine.handle(req(3, "baselineStep1"));
    reply = engine.handle(req(4, "baselineStep3"));
    CHECK_FALSE(reply.ok);
    CHECK(reply.errorCode == "baselineOrderWrong");

    // commit with steps missing
    reply = engine.handle(req(5, "baselineCommit"));
    CHECK_FALSE(reply.ok);
    CHECK(reply.errorCode == "baselineOrderWrong");
}

TEST_CASE("full cycle: baseline, recipe, measure, result") {
    SimEngine engine;
    CHECK(engine.handle(req(1, "setRecipe", {{"name", "SiO2 on Si"}})).ok);
    runBaseline(engine);

    const auto reply = engine.handle(req(10, "measure"));
    REQUIRE(reply.ok);
    const auto result = f20::measureResultFromJson(reply.result);
    REQUIRE(result.has_value());
    CHECK(result->passed);
    CHECK(result->gof > 0.95);
    CHECK(result->layers.at(0).thicknessNm == doctest::Approx(512.7).epsilon(0.01));
}

TEST_CASE("magic recipe FAIL_GOF produces a FAIL verdict") {
    SimEngine engine;
    engine.handle(req(1, "setRecipe", {{"name", "FAIL_GOF"}}));
    runBaseline(engine);
    const auto reply = engine.handle(req(2, "measure"));
    REQUIRE(reply.ok);
    const auto result = f20::measureResultFromJson(reply.result);
    REQUIRE(result.has_value());
    CHECK_FALSE(result->passed);
    CHECK(result->gof < 0.95);
}

TEST_CASE("magic recipe REF_LOW fails baseline step 2 with referenceSignalBad") {
    SimEngine engine;
    engine.handle(req(1, "setRecipe", {{"name", "REF_LOW"}}));
    engine.handle(req(2, "baselineSetRefMat", {{"name", "Si"}}));
    engine.handle(req(3, "baselineStep1"));
    const auto reply = engine.handle(req(4, "baselineStep2"));
    CHECK_FALSE(reply.ok);
    CHECK(reply.errorCode == "referenceSignalBad");
}

TEST_CASE("unknown recipe is recipeNotFound") {
    SimEngine engine;
    const auto reply = engine.handle(req(1, "setRecipe", {{"name", "Nope"}}));
    CHECK_FALSE(reply.ok);
    CHECK(reply.errorCode == "recipeNotFound");
}

TEST_CASE("recover works only after a committed baseline (spec test 6)") {
    SimEngine engine;
    auto reply = engine.handle(req(1, "baselineRecover"));
    CHECK_FALSE(reply.ok);
    CHECK(reply.errorCode == "baselineRecoverFailed");

    engine.handle(req(2, "setRecipe", {{"name", "SiO2 on Si"}}));
    runBaseline(engine);
    // a "restart": recover must now succeed
    CHECK(engine.handle(req(3, "baselineRecover")).ok);
}

TEST_CASE("quit sets the flag that stops the server loop") {
    SimEngine engine;
    CHECK_FALSE(engine.quitRequested());
    CHECK(engine.handle(req(1, "quit")).ok);
    CHECK(engine.quitRequested());
}
