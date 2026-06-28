// Unit tests for the config layer: resource_limits and task_config, built from
// both YAML nodes (standalone mode) and cli::cli_options (compat mode).
// Construction is pure (no syscalls); invalid input exits(2).

#include "config.hpp"

#include <gtest/gtest.h>

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

    TEST(resource_limits_cli, maps_optional_fields) {
        cli::cli_options o;
        o.memory = 2048;
        o.cpu_time = 3;
        o.open_files = 64;
        o.file_size = 4096;

        config::resource_limits rl(o);
        EXPECT_EQ(rl.memory(), 2048u);
        EXPECT_EQ(rl.cpu_time(), 3u);
        EXPECT_EQ(rl.open_files(), 64u);
        EXPECT_EQ(rl.file_size(), 4096u);
        EXPECT_FALSE(rl.stack_size().has_value());
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
        YAML::Node t;
        t[config::config_options::task::TASK_ID] = "x";  // no cmd at all
        EXPECT_EXIT(
            { config::task_config tc(t); }, ::testing::ExitedWithCode(2),
            "Missing cmd");
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

}  // namespace
