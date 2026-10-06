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
