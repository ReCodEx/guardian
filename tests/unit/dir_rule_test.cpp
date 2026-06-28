// Unit tests for config::dir_rule — the box-fs directory-rule parser
// (inner[=outer][:opts]). Pure string/regex logic; invalid rules exit(2).

#include <gtest/gtest.h>

#include "config.hpp"

namespace {

    TEST(dir_rule, bare_inner_defaults_to_root_mount) {
        config::dir_rule r("etc");
        EXPECT_EQ(r.in_dir(), "etc");
        EXPECT_EQ(r.out_dir(), "/etc");  // outer defaults to "/" / inner
        EXPECT_FALSE(r.rw());
        EXPECT_FALSE(r.fs());
    }

    TEST(dir_rule, explicit_outer_and_option) {
        config::dir_rule r("proc=proc:fs");
        EXPECT_EQ(r.in_dir(), "proc");
        EXPECT_EQ(r.out_dir(), "/proc");
        EXPECT_TRUE(r.fs());
    }

    TEST(dir_rule, option_without_outer) {
        config::dir_rule r("lib64:maybe");
        EXPECT_EQ(r.in_dir(), "lib64");
        EXPECT_TRUE(r.maybe());
    }

    TEST(dir_rule, all_boolean_options_parse) {
        config::dir_rule r("box:rw:dev:noexec:norec:allow_newdir");
        EXPECT_TRUE(r.rw());
        EXPECT_TRUE(r.dev());
        EXPECT_TRUE(r.noexec());
        EXPECT_TRUE(r.norec());
        EXPECT_TRUE(r.allow_newdir());
    }

    TEST(dir_rule, tmp_implies_rw) {
        config::dir_rule r("work:tmp");
        EXPECT_TRUE(r.tmp());
        EXPECT_TRUE(r.rw());  // tmp sets rw_ as a side effect.
    }

    TEST(dir_rule, string_roundtrips_original) {
        config::dir_rule r("etc=etc:rw");
        EXPECT_EQ(r.string(), "etc=etc:rw");
    }

    // --- errors --------------------------------------------------------------

    TEST(dir_rule_errors, absolute_inner_rejected) {
        EXPECT_EXIT(
            { config::dir_rule r("/abs"); }, ::testing::ExitedWithCode(2),
            "Invalid inner path");
    }

    TEST(dir_rule_errors, empty_rule_rejected) {
        EXPECT_EXIT(
            { config::dir_rule r(""); }, ::testing::ExitedWithCode(2),
            "Invalid fs-rule syntax");
    }

    TEST(dir_rule_errors, leading_equals_rejected) {
        EXPECT_EXIT(
            { config::dir_rule r("=bad"); }, ::testing::ExitedWithCode(2),
            "Invalid fs-rule syntax");
    }

}  // namespace
