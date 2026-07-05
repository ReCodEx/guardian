// Unit tests for the hand-rolled CLI front-end (cli::parse, ADR 0002).
// Happy paths run in-process; usage errors exit(2) and are checked as death
// tests against the message on stderr.

#include "cli_options.hpp"

#include <gtest/gtest.h>

#include <vector>

namespace {

    // Build an argv (with a dummy argv[0]) and parse it. The string literals
    // have static storage, so the const_cast pointers stay valid for the call.
    cli::cli_options parse_args(const std::vector<const char*>& args) {
        std::vector<char*> argv;
        argv.push_back(const_cast<char*>("isolator"));
        for (auto* a : args) {
            argv.push_back(const_cast<char*>(a));
        }
        argv.push_back(nullptr);
        return cli::parse(static_cast<int>(argv.size()) - 1, argv.data());
    }

    // --- modes ---------------------------------------------------------------

    TEST(cli_mode, run_with_program_and_args) {
        auto o =
            parse_args({"--run", "--box-id=3", "--", "/bin/echo", "hi", "x"});
        EXPECT_EQ(o.mode, cli::run_mode::run);
        ASSERT_TRUE(o.box_id.has_value());
        EXPECT_EQ(*o.box_id, 3u);
        EXPECT_EQ(o.program, "/bin/echo");
        ASSERT_EQ(o.args.size(), 2u);
        EXPECT_EQ(o.args[0], "hi");
        EXPECT_EQ(o.args[1], "x");
    }

    TEST(cli_mode, init_and_cleanup) {
        EXPECT_EQ(parse_args({"--init", "--box-id=0"}).mode,
                  cli::run_mode::init);
        EXPECT_EQ(parse_args({"--cleanup", "--box-id=0"}).mode,
                  cli::run_mode::cleanup);
    }

    TEST(cli_mode, standalone_yaml) {
        auto o = parse_args({"--yaml=/tmp/cfg.yml"});
        EXPECT_EQ(o.mode, cli::run_mode::standalone);
        ASSERT_TRUE(o.yaml.has_value());
        EXPECT_EQ(*o.yaml, "/tmp/cfg.yml");
    }

    // --- compat flag aliases map onto semantic fields ------------------------

    TEST(cli_aliases, cg_mem_and_mem_both_set_memory) {
        EXPECT_EQ(*parse_args({"--run", "--box-id=0", "--cg-mem=2048", "--",
                               "/bin/true"})
                       .memory,
                  2048u);
        EXPECT_EQ(*parse_args(
                       {"--run", "--box-id=0", "--mem=4096", "--", "/bin/true"})
                       .memory,
                  4096u);
    }

    TEST(cli_time, fractional_seconds_parse) {
        // The Worker emits --time=1.000000 (std::to_string of a float), so the
        // time flags must accept fractional seconds, not exit(2).
        auto o = parse_args({"--run", "--box-id=0", "--time=1.5",
                             "--wall-time=2.25", "--extra-time=0.5", "--",
                             "/bin/true"});
        EXPECT_DOUBLE_EQ(*o.cpu_time, 1.5);
        EXPECT_DOUBLE_EQ(*o.wall_time, 2.25);
        EXPECT_DOUBLE_EQ(*o.extra_time, 0.5);
    }

    TEST(cli_aliases, time_maps_to_cpu_time) {
        auto o =
            parse_args({"--run", "--box-id=0", "--time=10", "--", "/bin/true"});
        EXPECT_EQ(*o.cpu_time, 10u);
    }

    // --- --processes tri-state -----------------------------------------------

    TEST(cli_processes, absent_is_nullopt) {
        auto o = parse_args({"--run", "--box-id=0", "--", "/bin/true"});
        EXPECT_FALSE(o.processes.has_value());
    }

    TEST(cli_processes, bare_means_unlimited_zero) {
        auto o = parse_args(
            {"--run", "--box-id=0", "--processes", "--", "/bin/true"});
        ASSERT_TRUE(o.processes.has_value());
        EXPECT_EQ(*o.processes, 0u);
    }

    TEST(cli_processes, with_value) {
        auto o = parse_args(
            {"--run", "--box-id=0", "--processes=8", "--", "/bin/true"});
        EXPECT_EQ(*o.processes, 8u);
    }

    // --- repeatable rules + flags --------------------------------------------

    TEST(cli_repeatable, env_and_dir_accumulate) {
        auto o =
            parse_args({"--run", "--box-id=0", "--env=PATH", "--env=HOME=/h",
                        "--dir=etc", "--dir=proc=proc:fs", "--", "/bin/true"});
        ASSERT_EQ(o.env_rules.size(), 2u);
        EXPECT_EQ(o.env_rules[0], "PATH");
        EXPECT_EQ(o.env_rules[1], "HOME=/h");
        ASSERT_EQ(o.dir_rules.size(), 2u);
        EXPECT_EQ(o.dir_rules[1], "proc=proc:fs");
    }

    TEST(cli_flags, share_net_and_cosmetic_flags) {
        auto o = parse_args({"--run", "--box-id=0", "--cg", "--cg-timing",
                             "--share-net", "--", "/bin/true"});
        EXPECT_TRUE(
            o.share_net);  // --cg / --cg-timing are accepted and ignored.
    }

    // --- usage errors: exit(2) + message -------------------------------------

    TEST(cli_errors, unknown_option) {
        EXPECT_EXIT(
            { parse_args({"--bogus"}); }, ::testing::ExitedWithCode(2),
            "Unknown option");
    }

    TEST(cli_errors, missing_required_value) {
        EXPECT_EXIT(
            { parse_args({"--box-id"}); }, ::testing::ExitedWithCode(2),
            "Missing value");
    }

    TEST(cli_errors, garbage_numeric_value) {
        EXPECT_EXIT(
            { parse_args({"--run", "--box-id=abc", "--", "/bin/true"}); },
            ::testing::ExitedWithCode(2), "Invalid numeric value");
    }

    TEST(cli_errors, conflicting_modes) {
        EXPECT_EXIT(
            { parse_args({"--init", "--cleanup", "--box-id=0"}); },
            ::testing::ExitedWithCode(2), "Conflicting mode");
    }

    TEST(cli_errors, no_mode_selected) {
        EXPECT_EXIT(
            { parse_args({"--debug"}); }, ::testing::ExitedWithCode(2),
            "No mode selected");
    }

    TEST(cli_errors, run_without_program) {
        EXPECT_EXIT(
            { parse_args({"--run", "--box-id=0"}); },
            ::testing::ExitedWithCode(2), "requires a program");
    }

    TEST(cli_errors, program_without_run) {
        EXPECT_EXIT(
            { parse_args({"--init", "--box-id=0", "--", "/bin/true"}); },
            ::testing::ExitedWithCode(2),
            "Program arguments are only valid with --run");
    }

    TEST(cli_errors, box_id_required_for_compat) {
        EXPECT_EXIT(
            { parse_args({"--run", "--", "/bin/true"}); },
            ::testing::ExitedWithCode(2), "--box-id is required");
    }

}  // namespace
