// Unit tests for the pure parts of the disk-quota helper: the byte-to-block
// rounding standalone `disk-usage` goes through, and what counts as asking for
// no cap. Setting a quota needs root and a quota filesystem, so it is covered by
// the workload tier (tests/workload/test_quota.py).

#include "quota.hpp"

#include <gtest/gtest.h>

namespace {

    // A non-zero byte count rounds *up*, so a small cap never becomes the 0
    // that the kernel reads as unlimited.
    TEST(quota_blocks_for_bytes, rounds_up_to_whole_quota_blocks) {
        EXPECT_EQ(quota::blocks_for_bytes(0), 0u);
        EXPECT_EQ(quota::blocks_for_bytes(1), 1u);
        EXPECT_EQ(quota::blocks_for_bytes(500), 1u);
        EXPECT_EQ(quota::blocks_for_bytes(1024), 1u);
        EXPECT_EQ(quota::blocks_for_bytes(1025), 2u);
        EXPECT_EQ(quota::blocks_for_bytes(1048576), 1024u);
    }

    TEST(quota_limits, unlimited_only_when_both_caps_are_zero) {
        EXPECT_TRUE((quota::limits{}).unlimited());
        EXPECT_TRUE((quota::limits{.blocks = 0, .inodes = 0}).unlimited());
        EXPECT_FALSE((quota::limits{.blocks = 1, .inodes = 0}).unlimited());
        EXPECT_FALSE((quota::limits{.blocks = 0, .inodes = 1}).unlimited());
    }

}  // namespace
