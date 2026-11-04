#include "woodyn/core/FixedTimestep.hpp"

#include <gtest/gtest.h>

using woodyn::FixedTimestep;

TEST(FixedTimestep, RunsWholeStepsAndKeepsRemainder) {
    FixedTimestep ts(0.01f);
    EXPECT_EQ(ts.accumulate(0.025f), 2);
    EXPECT_NEAR(ts.alpha(), 0.5f, 1e-4f);
    EXPECT_EQ(ts.accumulate(0.005f), 1);
    EXPECT_NEAR(ts.alpha(), 0.0f, 1e-4f);
}

TEST(FixedTimestep, CapsStepsAfterLongFrame) {
    FixedTimestep ts(0.01f, 4);
    EXPECT_EQ(ts.accumulate(1.0f), 4);
    // Backlog is dropped, not carried into the next frame.
    EXPECT_EQ(ts.accumulate(0.0f), 0);
}

TEST(FixedTimestep, IgnoresNegativeTime) {
    FixedTimestep ts(0.01f);
    EXPECT_EQ(ts.accumulate(-1.0f), 0);
    EXPECT_FLOAT_EQ(ts.alpha(), 0.0f);
}

TEST(FixedTimestep, RejectsInvalidStep) {
    EXPECT_THROW(FixedTimestep(0.0f), std::invalid_argument);
}
