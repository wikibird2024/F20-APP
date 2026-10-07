#include "doctest.h"
#include "f20/results.h"

using namespace f20;

TEST_CASE("measure result round-trip, optional fields preserved") {
    MeasureResult result;
    LayerResult layer;
    layer.layer = 1;
    layer.thicknessNm = 512.3;
    layer.n = 1.46;
    result.layers.push_back(layer);
    result.gof = 0.987;
    result.summary = "ok";
    result.passed = true;

    const auto back = measureResultFromJson(toJson(result));
    REQUIRE(back.has_value());
    REQUIRE(back->layers.size() == 1);
    CHECK(back->layers[0].thicknessNm == doctest::Approx(512.3));
    REQUIRE(back->layers[0].n.has_value());
    CHECK(*back->layers[0].n == doctest::Approx(1.46));
    CHECK_FALSE(back->layers[0].k.has_value()); // absent stays absent
    CHECK(back->passed);
}

TEST_CASE("spectrum with mismatched arrays is rejected") {
    json j{{"wavelengthNm", {400.0, 500.0}}, {"reflectance", {0.2}}};
    CHECK_FALSE(spectrumFromJson(j).has_value());
}

TEST_CASE("wrong types make a result malformed, never throw") {
    CHECK_FALSE(measureResultFromJson(json::parse(
        R"({"layers":[{"thicknessNm":"512"}],"gof":0.9,"passed":true})")).has_value());
    CHECK_FALSE(measureResultFromJson(json::parse(
        R"({"layers":[{"thicknessNm":512,"n":"1.4"}],"gof":0.9,"passed":true})")).has_value());
    CHECK_FALSE(measureResultFromJson(json::parse(
        R"({"layers":[{"thicknessNm":512,"layer":"1"}],"gof":0.9,"passed":true})")).has_value());
    CHECK_FALSE(measureResultFromJson(json::parse(
        R"({"layers":[5],"gof":0.9,"passed":true})")).has_value());
    CHECK_FALSE(measureResultFromJson(json::parse(
        R"({"layers":[],"gof":"0.9","passed":true})")).has_value());
    CHECK_FALSE(measureResultFromJson(json::parse("[1]")).has_value());
}

TEST_CASE("a spectrum with a non-number element is rejected") {
    json j{{"wavelengthNm", {400.0, nullptr}}, {"reflectance", {0.2, 0.3}}};
    CHECK_FALSE(spectrumFromJson(j).has_value());
}
