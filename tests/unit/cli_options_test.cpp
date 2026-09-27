// Unit tests for the hand-rolled CLI front-end (cli::parse).
// Happy paths run in-process; usage errors exit(2) and are checked as death
// tests against the message on stderr.

#include "cli_options.hpp"

#include <gtest/gtest.h>

#include <string>
#include <vector>

namespace {

    // Build an argv (with a dummy argv[0]) and parse it. The string literals
    // have static storage, so the const_cast pointers stay valid for the call.
    cli::cli_options parse_args(const std::vector<const char*>& args) {
        std::vector<char*> argv;
        argv.push_back(const_cast<char*>("recodex-guardian"));
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

    // --- zero-valued caps that cannot mean anything are refused (#22) ---------

    // Isolate reads an explicit 0 for these as "no limit"; we refuse it instead,
    // since enforcing it literally is meaningless (a 0 stack cannot exec, and a
    // process cap of 0 is just =1). Only the bare --processes above, and an
    // omitted flag, mean unlimited.

    TEST(cli_processes, explicit_zero_rejected) {
        EXPECT_EXIT(
            {
                parse_args(
                    {"--run", "--box-id=0", "--processes=0", "--", "/bin/true"});
            },
            ::testing::ExitedWithCode(2), "not a limit");
    }

    TEST(cli_stack, zero_rejected) {
        EXPECT_EXIT(
            {
                parse_args(
                    {"--run", "--box-id=0", "--stack=0", "--", "/bin/true"});
            },
            ::testing::ExitedWithCode(2), "not a limit");
    }

    TEST(cli_stack, with_value) {
        auto o = parse_args(
            {"--run", "--box-id=0", "--stack=64", "--", "/bin/true"});
        ASSERT_TRUE(o.stack.has_value());
        EXPECT_EQ(*o.stack, 64u);
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

    // --- pinning: cpuset lists ------------------------------------

    TEST(cli_cpuset, absent_is_nullopt) {
        auto o = parse_args({"--run", "--box-id=0", "--", "/bin/true"});
        EXPECT_FALSE(o.cpuset_cpus.has_value());
        EXPECT_FALSE(o.cpuset_mems.has_value());
    }

    TEST(cli_cpuset, lists_kept_verbatim) {
        for (const std::string list : {"2", "0,2,4", "1-4,6", "3-3", "007"}) {
            SCOPED_TRACE(list);
            const std::string cpus = "--cpuset-cpus=" + list;
            const std::string mems = "--cpuset-mems=" + list;
            auto o = parse_args({"--run", "--box-id=0", cpus.c_str(),
                                 mems.c_str(), "--", "/bin/true"});
            ASSERT_TRUE(o.cpuset_cpus.has_value());
            EXPECT_EQ(*o.cpuset_cpus, list);
            ASSERT_TRUE(o.cpuset_mems.has_value());
            EXPECT_EQ(*o.cpuset_mems, list);
        }
    }

    TEST(cli_cpuset, flags_are_independent) {
        auto o = parse_args(
            {"--run", "--box-id=0", "--cpuset-mems=1", "--", "/bin/true"});
        EXPECT_FALSE(o.cpuset_cpus.has_value());
        ASSERT_TRUE(o.cpuset_mems.has_value());
        EXPECT_EQ(*o.cpuset_mems, "1");
    }

    // The Worker may send one flag set to every phase; only --run uses it.
    TEST(cli_cpuset, accepted_on_init_and_cleanup) {
        EXPECT_EQ(*parse_args({"--init", "--box-id=0", "--cpuset-cpus=2"})
                       .cpuset_cpus,
                  "2");
        EXPECT_EQ(*parse_args({"--cleanup", "--box-id=0", "--cpuset-mems=0"})
                       .cpuset_mems,
                  "0");
    }

    // Stricter than the kernel: no strides, `N` or `all`, and an empty list
    // (the kernel's "inherit the parent's set") is refused, not read as
    // unpinned.
    TEST(cli_cpuset, malformed_lists_rejected) {
        for (const std::string bad :
             {"", ",", "1,", ",1", "1,,2", "3-1", "1-", "-1", " 1", "1 ",
              "1-2-3", "0-7:2/4", "all", "0-N", "+1", "a",
              "99999999999999999999"}) {
            SCOPED_TRACE(bad);
            const std::string flag = "--cpuset-cpus=" + bad;
            EXPECT_EXIT(
                {
                    parse_args(
                        {"--run", "--box-id=0", flag.c_str(), "--", "/bin/true"});
                },
                ::testing::ExitedWithCode(2),
                "Invalid cpuset list for --cpuset-cpus");
        }
    }

    TEST(cli_cpuset, mems_list_checked_too) {
        EXPECT_EXIT(
            {
                parse_args({"--run", "--box-id=0", "--cpuset-mems=0-", "--",
                            "/bin/true"});
            },
            ::testing::ExitedWithCode(2),
            "Invalid cpuset list for --cpuset-mems");
    }

    // A malformed list fails at parse time whatever the phase, not only when
    // --run would write it.
    TEST(cli_cpuset, checked_in_every_phase) {
        EXPECT_EXIT(
            { parse_args({"--init", "--box-id=0", "--cpuset-cpus=abc"}); },
            ::testing::ExitedWithCode(2),
            "Invalid cpuset list for --cpuset-cpus");
    }

    // --- disk quota: --quota=<blocks>,<inodes> -------------------------------

    TEST(cli_quota, absent_is_nullopt) {
        EXPECT_FALSE(parse_args({"--init", "--box-id=0"}).quota.has_value());
    }

    TEST(cli_quota, blocks_and_inodes) {
        auto o = parse_args({"--init", "--box-id=0", "--quota=1048576,100"});
        ASSERT_TRUE(o.quota.has_value());
        EXPECT_EQ(o.quota->blocks, 1048576u);
        EXPECT_EQ(o.quota->inodes, 100u);
    }

    // A 0 leaves that one cap unlimited — the kernel's own reading of a 0
    // limit — so neither number refuses it, and each stands alone.
    TEST(cli_quota, zeros_accepted) {
        auto inodes_only = parse_args({"--init", "--box-id=0", "--quota=0,100"});
        EXPECT_EQ(inodes_only.quota->blocks, 0u);
        EXPECT_EQ(inodes_only.quota->inodes, 100u);

        auto blocks_only =
            parse_args({"--init", "--box-id=0", "--quota=1024,0"});
        EXPECT_EQ(blocks_only.quota->blocks, 1024u);
        EXPECT_EQ(blocks_only.quota->inodes, 0u);

        EXPECT_TRUE(parse_args({"--init", "--box-id=0", "--quota=0,0"})
                        .quota->unlimited());
    }

    // The Worker sends it on --init only; --run applies it too and --cleanup
    // ignores it, but every phase parses it.
    TEST(cli_quota, accepted_in_every_phase) {
        EXPECT_TRUE(parse_args({"--run", "--box-id=0", "--quota=1,1", "--",
                                "/bin/true"})
                        .quota.has_value());
        EXPECT_TRUE(parse_args({"--cleanup", "--box-id=0", "--quota=1,1"})
                        .quota.has_value());
    }

    TEST(cli_quota, malformed_rejected) {
        for (const std::string bad :
             {"", ",", "100", "100,", ",100", "1,2,3", "a,1", "1,b", "-1,1",
              "+1,1", " 1,1", "1, 1", "1,1 ", "99999999999999999999,1"}) {
            SCOPED_TRACE(bad);
            const std::string flag = "--quota=" + bad;
            EXPECT_EXIT(
                { parse_args({"--init", "--box-id=0", flag.c_str()}); },
                ::testing::ExitedWithCode(2), "Invalid quota for --quota");
        }
    }

    // Like the cpuset lists: a malformed value fails in a phase that would
    // ignore it, too.
    TEST(cli_quota, checked_in_every_phase) {
        EXPECT_EXIT(
            { parse_args({"--cleanup", "--box-id=0", "--quota=abc"}); },
            ::testing::ExitedWithCode(2), "Invalid quota for --quota");
    }

    // --disk-usage was the Guardian's own quota flag before --quota was wired;
    // compatibility mode sets the quota through --quota alone.
    TEST(cli_quota, disk_usage_flag_removed) {
        EXPECT_EXIT(
            {
                parse_args({"--run", "--box-id=0", "--disk-usage=1024", "--",
                            "/bin/true"});
            },
            ::testing::ExitedWithCode(2), "Unknown option");
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
