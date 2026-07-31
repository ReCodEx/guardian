// Unit tests for config::env_rule — the environment-rule parser
// (NAME | NAME=VALUE | full-env=true|false). Invalid full-env values exit(2).

#include <gtest/gtest.h>

#include "config.hpp"

namespace {

    TEST(env_rule, inherit_by_name) {
        config::env_rule r("PATH");
        ASSERT_TRUE(r.inherited_var().has_value());
        EXPECT_EQ(*r.inherited_var(), "PATH");
        EXPECT_FALSE(r.name_value_pair().has_value());
        EXPECT_FALSE(r.full_env());
    }

    TEST(env_rule, name_value_pair) {
        config::env_rule r("HOME=/home/box");
        ASSERT_TRUE(r.name_value_pair().has_value());
        const auto& [name, value] = *r.name_value_pair();
        EXPECT_EQ(name, "HOME");
        EXPECT_EQ(value, "/home/box");
        EXPECT_FALSE(r.inherited_var().has_value());
    }

    TEST(env_rule, empty_value_is_a_pair_not_an_inherit) {
        config::env_rule r("FOO=");
        ASSERT_TRUE(r.name_value_pair().has_value());
        const auto& [name, value] = *r.name_value_pair();
        EXPECT_EQ(name, "FOO");
        EXPECT_EQ(value, "");
    }

    TEST(env_rule, value_may_contain_equals) {
        config::env_rule r("K=a=b");  // split on first '='
        ASSERT_TRUE(r.name_value_pair().has_value());
        const auto& [name, value] = *r.name_value_pair();
        EXPECT_EQ(name, "K");
        EXPECT_EQ(value, "a=b");
    }

    TEST(env_rule, full_env_true_and_false) {
        EXPECT_TRUE(config::env_rule("full-env=true").full_env());
        EXPECT_FALSE(config::env_rule("full-env=false").full_env());
    }

    TEST(env_rule_errors, full_env_garbage_rejected) {
        EXPECT_EXIT(
            { config::env_rule r("full-env=maybe"); },
            ::testing::ExitedWithCode(2), "Invalid value in environment rule");
    }

}  // namespace
