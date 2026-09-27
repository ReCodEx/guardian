// Unit tests for the config layer: resource_limits and task_config, built from
// both YAML nodes (standalone mode) and cli::cli_options (compat mode).
// Construction is pure (no syscalls); invalid input exits(2).

#include "config.hpp"

#include <gtest/gtest.h>

#include <vector>

#include "yaml-cpp/yaml.h"

namespace {

    // --- resource_limits from a YAML node ------------------------------------

    TEST(resource_limits_yaml, reads_named_limits) {
        YAML::Node n;
        n[config::config_options::task::CPU_TIME] = 5;
        n[config::config_options::task::MEMORY] = 1048576;
        n[config::config_options::task::STACK] = 8192;
        n[config::config_options::task::WALL_TIME] = 30;
        n[config::config_options::task::PROCESSES] = 16;

        config::resource_limits rl(n);
        EXPECT_EQ(rl.cpu_time(), 5u);
        EXPECT_EQ(rl.memory(), 1048576u);
        EXPECT_EQ(rl.stack_size(), 8192u);
        EXPECT_EQ(rl.wall_time(), 30u);
        EXPECT_EQ(rl.processes(), 16u);
    }

    TEST(resource_limits_yaml,
         absent_limits_stay_unset_with_default_wall_time) {
        YAML::Node n;
        n[config::config_options::task::CPU_TIME] = 7;
        config::resource_limits rl(n);
        EXPECT_EQ(rl.cpu_time(), 7u);
        EXPECT_FALSE(rl.memory().has_value());
        EXPECT_EQ(rl.wall_time(), 60u);  // DEFAULT_WALL_TIME
    }

    TEST(resource_limits_yaml, garbage_value_rejected) {
        YAML::Node n;
        n[config::config_options::task::CPU_TIME] = "not-a-number";
        EXPECT_EXIT(
            { config::resource_limits rl(n); }, ::testing::ExitedWithCode(2),
            "Invalid resource limit value");
    }

    // --- resource_limits from cli_options ------------------------------------

    TEST(resource_limits_cli, converts_isolate_units_to_canonical) {
        // The compat ctor normalizes Isolate's flag units into canonical form:
        // KB sizes -> bytes, fractional seconds kept as double, counts as-is.
        cli::cli_options o;
        o.memory = 2048;     // --cg-mem, KB
        o.file_size = 4096;  // --fsize, KB
        o.stack = 64;        // --stack, KB
        o.cpu_time = 1.5;    // --time, seconds
        o.wall_time = 2.5;   // --wall-time, seconds
        o.open_files = 64;   // count, not a size

        config::resource_limits rl(o);
        EXPECT_EQ(rl.memory(), 2048u * 1024);  // KB -> bytes
        EXPECT_EQ(rl.file_size(), 4096u * 1024);
        EXPECT_EQ(rl.stack_size(), 64u * 1024);
        EXPECT_DOUBLE_EQ(rl.cpu_time().value(), 1.5);  // fractional seconds
        EXPECT_DOUBLE_EQ(rl.wall_time(), 2.5);
        EXPECT_EQ(rl.open_files(), 64u);  // count, unchanged
    }

    // --- zero-valued process cap means "no limit" (#21) -----------------------

    // The bare `--processes` the Worker emits parses to 0 and means *unlimited*,
    // so it must leave the cap unset — a literal pids.max = 0 would let nothing
    // fork and break every real job. An explicit 0 never reaches here: it is
    // refused at parse (cli_options_test) / on load below.

    TEST(resource_limits_cli, bare_processes_leaves_no_process_cap) {
        cli::cli_options o;
        o.processes = 0;  // what a bare `--processes` parses to
        config::resource_limits rl(o);
        EXPECT_FALSE(rl.processes().has_value());
    }

    TEST(resource_limits_cli, positive_processes_is_kept) {
        cli::cli_options o;
        o.processes = 4;
        config::resource_limits rl(o);
        ASSERT_TRUE(rl.processes().has_value());
        EXPECT_EQ(*rl.processes(), 4u);
    }

    TEST(resource_limits_yaml, zero_processes_rejected) {
        YAML::Node n;
        n[config::config_options::task::PROCESSES] = 0;
        EXPECT_EXIT({ config::resource_limits rl(n); },
                    ::testing::ExitedWithCode(2), "must be non-zero");
    }

    // --- unset stack cap means unlimited; a zero one is refused (#22) ---------

    // An *unset* stack cap means unlimited — Isolate always applies
    // RLIMIT_STACK, using RLIM_INFINITY when none is given
    // (isolate/isolate.c:803) — which the enforcement side turns into infinity.
    // A literal 0 would leave the task no stack at all, so it is refused rather
    // than silently read as unlimited.

    TEST(resource_limits_yaml, absent_stack_leaves_no_stack_cap) {
        YAML::Node n;
        n[config::config_options::task::CPU_TIME] = 1;
        config::resource_limits rl(n);
        EXPECT_FALSE(rl.stack_size().has_value());
    }

    TEST(resource_limits_yaml, zero_stack_rejected) {
        YAML::Node n;
        n[config::config_options::task::STACK] = 0;
        EXPECT_EXIT({ config::resource_limits rl(n); },
                    ::testing::ExitedWithCode(2), "must be non-zero");
    }

    // --- task_config from a YAML node ----------------------------------------

    TEST(task_config_yaml, parses_id_exec_and_args) {
        YAML::Node t;
        t[config::config_options::task::TASK_ID] = "compile";
        t[config::config_options::task::CMD]
         [config::config_options::task::EXEC_PATH] = "/usr/bin/gcc";
        t[config::config_options::task::CMD]
         [config::config_options::task::EXEC_ARGS]
             .push_back("-O2");
        t[config::config_options::task::CMD]
         [config::config_options::task::EXEC_ARGS]
             .push_back("main.c");

        config::task_config tc(t);
        EXPECT_EQ(tc.name(), "compile");
        EXPECT_EQ(tc.exec_path(), "/usr/bin/gcc");
        // argv[0] is the executable, prepended to the parsed args.
        ASSERT_EQ(tc.exec_args().size(), 3u);
        EXPECT_EQ(tc.exec_args()[0], "/usr/bin/gcc");
        EXPECT_EQ(tc.exec_args()[1], "-O2");
        EXPECT_EQ(tc.exec_args()[2], "main.c");
    }

    TEST(task_config_yaml, missing_task_id_rejected) {
        YAML::Node t;
        t[config::config_options::task::CMD]
         [config::config_options::task::EXEC_PATH] = "/bin/true";
        EXPECT_EXIT(
            { config::task_config tc(t); }, ::testing::ExitedWithCode(2),
            "Missing task id");
    }

    TEST(task_config_yaml, missing_cmd_rejected) {
        // `cmd` entirely absent: the `!cmd` branch must short-circuit before
        // indexing into it (which would throw YAML::InvalidNode) and report the
        // same single "missing executable" error as a present-but-empty cmd.
        YAML::Node t;
        t[config::config_options::task::TASK_ID] = "x";
        EXPECT_EXIT(
            { config::task_config tc(t); }, ::testing::ExitedWithCode(2),
            "Missing path to executable");
    }

    TEST(task_config_yaml, missing_exec_rejected) {
        YAML::Node t;
        t[config::config_options::task::TASK_ID] = "x";
        // `cmd` present (so the nested access doesn't throw) but without `bin`.
        t[config::config_options::task::CMD]
         [config::config_options::task::EXEC_ARGS]
             .push_back("arg");
        EXPECT_EXIT(
            { config::task_config tc(t); }, ::testing::ExitedWithCode(2),
            "Missing path to executable");
    }

    // --- task_config from cli_options ----------------------------------------

    TEST(task_config_cli, builds_from_program_and_args) {
        cli::cli_options o;
        o.program = "/bin/echo";
        o.args = {"hello"};

        config::task_config tc(o);
        EXPECT_EQ(tc.exec_path(), "/bin/echo");
        ASSERT_EQ(tc.exec_args().size(), 2u);
        EXPECT_EQ(tc.exec_args()[0], "/bin/echo");
        EXPECT_EQ(tc.exec_args()[1], "hello");
    }

    TEST(task_config_cli, empty_program_rejected) {
        cli::cli_options o;  // program left empty
        EXPECT_EXIT(
            { config::task_config tc(o); }, ::testing::ExitedWithCode(2),
            "No path to executable");
    }

    // --- directory rules: option parsing -------------------------------------

    TEST(dir_rule, dev_norec_sets_dev_and_norec) {
        config::dir_rule r("dev:dev:norec");
        EXPECT_EQ(r.in_dir(), "dev");
        EXPECT_EQ(r.out_dir(), "/dev");
        EXPECT_TRUE(r.dev());
        EXPECT_TRUE(r.norec());
        EXPECT_FALSE(r.fs());
    }

    TEST(dir_rule, dev_shm_is_rw_tmpfs) {
        // The outer token is the filesystem type for an fs rule; out_dir keeps
        // the leading slash and the mount code strips it back to "tmpfs".
        config::dir_rule r("dev/shm=tmpfs:fs:rw");
        EXPECT_EQ(r.in_dir(), "dev/shm");
        EXPECT_EQ(r.out_dir(), "/tmpfs");
        EXPECT_TRUE(r.fs());
        EXPECT_TRUE(r.rw());
    }

    TEST(dir_rule, tmp_flag_forces_rw) {
        config::dir_rule r("tmp:tmp");
        EXPECT_EQ(r.in_dir(), "tmp");
        EXPECT_TRUE(r.tmp());
        EXPECT_TRUE(r.rw());  // the tmp flag implies rw
        EXPECT_FALSE(r.fs());
    }

    TEST(dir_rule, strips_leading_slash_from_inner) {
        // The Worker sends absolute inner paths (--dir=/etc/java); they must be
        // sanitized to box-relative, matching Isolate's sanitize_dir_path.
        config::dir_rule bare("/etc/java");
        EXPECT_EQ(bare.in_dir(), "etc/java");
        EXPECT_EQ(bare.out_dir(), "/etc/java");  // outer defaults to the host path

        config::dir_rule tmp("/tmp:tmp");
        EXPECT_EQ(tmp.in_dir(), "tmp");
        EXPECT_TRUE(tmp.tmp());
    }

    TEST(dir_rule, rejects_dotdot_escape) {
        EXPECT_EXIT({ config::dir_rule r(".."); },
                    ::testing::ExitedWithCode(2), "Invalid inner path");
        EXPECT_EXIT({ config::dir_rule r("foo/../../etc"); },
                    ::testing::ExitedWithCode(2), "Invalid inner path");
    }

    // --- compat --dir wiring + override --------------------------------------

    TEST(box_fs_config_cli, dir_rules_become_user_rules) {
        cli::cli_options o;
        o.dir_rules = {"/opt/dotnet", "etc/alternatives=/etc/alternatives:maybe"};
        config::box_fs_config fs(o);

        ASSERT_EQ(fs.rules().size(), 2u);
        EXPECT_EQ(fs.rules()[0].in_dir(), "opt/dotnet");
        EXPECT_EQ(fs.rules()[1].in_dir(), "etc/alternatives");
        EXPECT_TRUE(fs.rules()[1].maybe());
    }

    TEST(box_fs_config_cli, user_rule_overrides_same_inner_default) {
        // The Worker's --dir=/tmp:tmp collides with the default tmp:tmp: the
        // user rule must replace the default, not stack a second mount.
        cli::cli_options o;
        o.dir_rules = {"/tmp:tmp"};
        config::box_fs_config fs(o);

        auto has_inner = [](const auto& rules, const char* in) {
            return std::any_of(rules.begin(), rules.end(),
                               [&](const config::dir_rule& r) {
                                   return r.in_dir() == in;
                               });
        };
        EXPECT_FALSE(has_inner(fs.default_rules(), "tmp"));  // default dropped
        EXPECT_TRUE(has_inner(fs.rules(), "tmp"));           // user rule present
    }

    // --- default dir set: pinned against Isolate's built-in set ---------------

    TEST(box_fs_config_cli, default_set_matches_isolate_plus_etc) {
        cli::cli_options o;
        config::box_fs_config fs(o);

        ASSERT_TRUE(fs.use_default_rules());
        const auto& rules = fs.default_rules();

        // Isolate's init_dir_rules() set (rules.c), minus box (our /box is the
        // pivot root's own subdir) plus our retained etc, in apply order.
        // dev must precede dev/shm (nested mount).
        ASSERT_EQ(rules.size(), 9u);
        EXPECT_EQ(rules[0].string(), "etc");
        EXPECT_EQ(rules[1].string(), "bin");
        EXPECT_EQ(rules[2].string(), "dev:dev:norec");
        EXPECT_EQ(rules[3].string(), "dev/shm=tmpfs:fs:rw");
        EXPECT_EQ(rules[4].string(), "lib");
        EXPECT_EQ(rules[5].string(), "lib64:maybe");
        EXPECT_EQ(rules[6].string(), "proc=proc:fs");
        EXPECT_EQ(rules[7].string(), "tmp:tmp");
        EXPECT_EQ(rules[8].string(), "usr");
    }

    // --- root_configuration: pinning reaches --run only -----------

    // Build a root_configuration from an argv (with a dummy argv[0]), as main()
    // does. The string literals have static storage, so the pointers stay
    // valid for the call.
    config::root_configuration root_config(
        const std::vector<const char*>& args) {
        std::vector<char*> argv;
        argv.push_back(const_cast<char*>("recodex-guardian"));
        for (auto* a : args) {
            argv.push_back(const_cast<char*>(a));
        }
        argv.push_back(nullptr);
        return config::root_configuration(static_cast<int>(argv.size()) - 1,
                                          argv.data());
    }

    TEST(root_configuration_cli, run_carries_pinning) {
        auto rc = root_config({"--run", "--box-id=0", "--cpuset-cpus=0-1",
                               "--cpuset-mems=0", "--", "/bin/true"});
        ASSERT_TRUE(rc.cpuset_cpus().has_value());
        EXPECT_EQ(*rc.cpuset_cpus(), "0-1");
        ASSERT_TRUE(rc.cpuset_mems().has_value());
        EXPECT_EQ(*rc.cpuset_mems(), "0");
    }

    TEST(root_configuration_cli, run_without_flags_is_unpinned) {
        auto rc = root_config({"--run", "--box-id=0", "--", "/bin/true"});
        EXPECT_FALSE(rc.cpuset_cpus().has_value());
        EXPECT_FALSE(rc.cpuset_mems().has_value());
    }

    // The box's cgroup exists only within a --run, so the other phases accept
    // the flags and drop them.
    TEST(root_configuration_cli, init_and_cleanup_ignore_pinning) {
        for (const char* mode : {"--init", "--cleanup"}) {
            SCOPED_TRACE(mode);
            auto rc = root_config(
                {mode, "--box-id=0", "--cpuset-cpus=2", "--cpuset-mems=0"});
            EXPECT_FALSE(rc.cpuset_cpus().has_value());
            EXPECT_FALSE(rc.cpuset_mems().has_value());
        }
    }

}  // namespace
