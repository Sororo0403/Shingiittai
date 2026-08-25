#include "core/MathUtils.h"

#include <cmath>
#include <iostream>
#include <string_view>

namespace {

int failures = 0;

void ExpectNear(float actual, float expected, std::string_view description) {
    constexpr float epsilon = 0.00001f;
    if (std::fabs(actual - expected) <= epsilon) {
        return;
    }

    std::cerr << "FAIL: " << description << " (expected " << expected
              << ", got " << actual << ")\n";
    ++failures;
}

void TestSmoothStep01() {
    ExpectNear(MathUtils::SmoothStep01(-1.0f), 0.0f,
               "SmoothStep01 clamps below zero");
    ExpectNear(MathUtils::SmoothStep01(0.0f), 0.0f,
               "SmoothStep01 starts at zero");
    ExpectNear(MathUtils::SmoothStep01(0.5f), 0.5f, "SmoothStep01 midpoint");
    ExpectNear(MathUtils::SmoothStep01(1.0f), 1.0f, "SmoothStep01 ends at one");
    ExpectNear(MathUtils::SmoothStep01(2.0f), 1.0f,
               "SmoothStep01 clamps above one");
}

void TestSmoothStepRange() {
    ExpectNear(MathUtils::SmoothStep(10.0f, 20.0f, 5.0f), 0.0f,
               "SmoothStep clamps below range");
    ExpectNear(MathUtils::SmoothStep(10.0f, 20.0f, 15.0f), 0.5f,
               "SmoothStep normalizes range");
    ExpectNear(MathUtils::SmoothStep(10.0f, 20.0f, 25.0f), 1.0f,
               "SmoothStep clamps above range");
    ExpectNear(MathUtils::SmoothStep(4.0f, 4.0f, 3.0f), 0.0f,
               "SmoothStep handles equal edges below");
    ExpectNear(MathUtils::SmoothStep(4.0f, 4.0f, 4.0f), 1.0f,
               "SmoothStep handles equal edges at edge");
}

} // namespace

int main() {
    TestSmoothStep01();
    TestSmoothStepRange();

    if (failures != 0) {
        std::cerr << failures << " test(s) failed.\n";
        return 1;
    }

    std::cout << "All EngineTests passed.\n";
    return 0;
}
