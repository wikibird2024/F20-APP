#include "doctest.h"
#include "f20/errors.h"

using namespace f20;

// Compare enums as ints: doctest's stringifier collides with our own
// toString(ErrorCode) found by argument-dependent lookup.
#define CHECK_ENUM_EQ(a, b) CHECK(static_cast<int>(a) == static_cast<int>(b))

TEST_CASE("every code maps to a name and back") {
    for (const auto code :
         {ErrorCode::measureNotReady, ErrorCode::baselineOrderWrong,
          ErrorCode::baselineRecoverFailed, ErrorCode::referenceSignalBad,
          ErrorCode::recipeNotFound, ErrorCode::fileOpenFailed,
          ErrorCode::layerOutOfBounds, ErrorCode::hardwareMissing,
          ErrorCode::busy, ErrorCode::filmeasureError}) {
        const auto back = errorCodeFromString(toString(code));
        REQUIRE(back.has_value());
        CHECK_ENUM_EQ(*back, code);
    }
}

TEST_CASE("unknown name gives nullopt, not a crash") {
    CHECK_FALSE(errorCodeFromString("somethingElse").has_value());
}

TEST_CASE("operator messages are plain sentences (spec 8.2)") {
    CHECK(operatorMessage(ErrorCode::measureNotReady) == "Run the baseline first");
    CHECK(operatorMessage(ErrorCode::recipeNotFound, "SiO2 on Si") ==
          "Recipe 'SiO2 on Si' not found in FILMeasure");
    // filmeasureError passes the raw detail through
    CHECK(operatorMessage(ErrorCode::filmeasureError, "boom") == "boom");
}
