#ifndef CONFIG
#define CONFIG

#include <filesystem>
#include <memory>
#include <optional>
#include <regex>
#include <tuple>
#include <vector>

#include "cli_options.hpp"
#include "terminate.hpp"
#include "utils.hpp"
#include "yaml-cpp/yaml.h"

namespace config {
    namespace fs = std::filesystem;

    class task_config;

    /// @brief Default directories
    namespace defaults {
        constexpr auto BOXES_DIR = "/isolate_boxes";
        constexpr auto BOXES_CGROUP = "/sys/fs/cgroup/isolate_boxes";
    }  // namespace defaults

    /// @brief Keywords for the configuration file
    namespace config_options {
        constexpr auto BOXES_DIR = "root-dir";
        constexpr auto BOXES_CGROUP = "root-cgroup";
        constexpr auto STATS_YAML = "stats-yaml";
        constexpr auto CONFIG_YAML = "yaml";
        constexpr auto SHARE_NET = "share-net";

        namespace credentials {
            constexpr auto INSTANCE_ID = "id";
            constexpr auto INSTANCE_NAME = "name";
            constexpr auto BOX_UID = "as-uid";
            constexpr auto BOX_GID = "as-gid";
        }  // namespace credentials

        constexpr auto TASKS = "tasks";
        namespace task {
            constexpr auto CMD = "cmd";
            constexpr auto TASK_ID = "task-id";
            constexpr auto EXEC_PATH = "bin";
            constexpr auto EXEC_ARGS = "args";
            constexpr auto STDIN_FILE = "stdin";
            constexpr auto STDOUT_FILE = "stdout";
            constexpr auto STDERR_FILE = "stderr";
            constexpr auto STDERR_TO_STDOUT = "stderr-to-stdout";
            constexpr auto CHDIR = "chdir";

            constexpr auto RLIMS = "limits";

            constexpr auto AS_SIZE = "as-size";
            constexpr auto CPU_TIME = "cpu-time";
            constexpr auto WALL_TIME = "wall-time";
            constexpr auto EXTRA_TIME = "extra-time";
            constexpr auto MEMORY = "mem";
            constexpr auto STACK = "stack";
            constexpr auto PROCESSES = "processes";
            constexpr auto DISK_USAGE = "disk-usage";
            constexpr auto OPEN_FILES = "open-files";
            constexpr auto FILE_SIZE = "fsize";
            constexpr auto CORE_DUMP_SIZE = "core";
        }  // namespace task

        constexpr auto ENV = "env";
        namespace env {
            constexpr auto ENV_VARS = "vars";
            constexpr auto INHERIT_ALL = "inherit-all";
        }  // namespace env

        constexpr auto BOX_FS = "box-fs";
        namespace box_fs {
            constexpr auto DIRECTORY_RULES = "dir-rules";
            constexpr auto USE_DEFAULT_DIR_RULES = "use-defaults";
            constexpr auto BOX_ROOT = "box-root";
        }  // namespace box_fs

    }  // namespace config_options

    /// @brief Keywords for the results file.
    namespace stats_names {
        constexpr auto STATUS = "status";
        constexpr auto OK = "OK";
        constexpr auto KILLED = "killed";
        constexpr auto NON_ZERO_EXIT_CODE = "non zero exit code";
        constexpr auto SIGNAL = "exitsig";
        constexpr auto EXIT_CODE = "exitcode";
        constexpr auto WALL_TIME_EXCEEDED = "wall-time";
        constexpr auto CPU_TIME_EXCEEDED = "cpu-time";
        constexpr auto MEMORY_EXCEEDED = "memory";
        constexpr auto WALL_TIME_S = "wall-time";
        constexpr auto CG_TOTAL_TIME_S = "time";
        constexpr auto CG_TOTAL_MEM_KB = "memory";
        constexpr auto RUSAGE_TOTAL_TIME_USEC = "rusage_total_time_usec";
        constexpr auto RUSAGE_TOTAL_MEM_BYTES = "rusage_total_mem_bytes";
    }  // namespace stats_names

    namespace yaml_utils {
        template <typename T>
        inline std::vector<T> get_vector(const YAML::Node& seq) {
            std::vector<T> v;
            for (std::size_t i = 0; i < seq.size(); i++) {
                v.emplace_back(seq[i].as<T>());
            }
            return v;
        }
    }  // namespace yaml_utils

    /// @brief Internal representation of resource limits (used by
    /// task_supervisor).
    class resource_limits {
       public:
        resource_limits() = default;

        /// @brief
        /// @param limits_node
        resource_limits(const YAML::Node& limits_node) {
            if (!limits_node) {
                set_defaults();
            } else {
                try {
                    if (limits_node[config_options::task::CPU_TIME]) {
                        cpu_time_ = limits_node[config_options::task::CPU_TIME]
                                        .as<size_t>();
                    }
                    if (limits_node[config_options::task::MEMORY]) {
                        memory_usage_ =
                            limits_node[config_options::task::MEMORY]
                                .as<size_t>();
                    }
                    if (limits_node[config_options::task::STACK]) {
                        stack_size_ = limits_node[config_options::task::STACK]
                                          .as<size_t>();
                    }
                    if (limits_node[config_options::task::WALL_TIME]) {
                        wall_time_ =
                            limits_node[config_options::task::WALL_TIME]
                                .as<size_t>();
                    }
                    if (limits_node[config_options::task::EXTRA_TIME]) {
                        extra_time_ =
                            limits_node[config_options::task::EXTRA_TIME]
                                .as<size_t>();
                    }
                    if (limits_node[config_options::task::PROCESSES]) {
                        processes_ =
                            limits_node[config_options::task::PROCESSES]
                                .as<size_t>();
                    }
                    if (limits_node[config_options::task::DISK_USAGE]) {
                        disk_usage_ =
                            limits_node[config_options::task::DISK_USAGE]
                                .as<size_t>();
                    }
                    if (limits_node[config_options::task::OPEN_FILES]) {
                        open_files_ =
                            limits_node[config_options::task::OPEN_FILES]
                                .as<size_t>();
                    }
                    if (limits_node[config_options::task::FILE_SIZE]) {
                        file_size_ =
                            limits_node[config_options::task::FILE_SIZE]
                                .as<size_t>();
                    }
                    if (limits_node[config_options::task::CORE_DUMP_SIZE]) {
                        core_size_ =
                            limits_node[config_options::task::CORE_DUMP_SIZE]
                                .as<size_t>();
                    }
                } catch (const YAML::BadConversion& e) {
                    YAML::Emitter node_string;
                    node_string << limits_node;
                    terminate("Invalid resource limit value: {}",
                              node_string.c_str());
                }
            }
        }

        resource_limits(const cli::cli_options& opts) {
            if (opts.cpu_time) {
                cpu_time_ = *opts.cpu_time;
            }
            if (opts.memory) {
                memory_usage_ = *opts.memory;
            }
            if (opts.stack) {
                stack_size_ = *opts.stack;
            }
            if (opts.wall_time) {
                wall_time_ = *opts.wall_time;
            }
            if (opts.extra_time) {
                extra_time_ = *opts.extra_time;
            }
            if (opts.processes) {
                processes_ = *opts.processes;
            }
            if (opts.as_size) {
                as_size_bytes_ = *opts.as_size;
            }
            if (opts.open_files) {
                open_files_ = *opts.open_files;
            }
            if (opts.file_size) {
                file_size_ = *opts.file_size;
            }
            if (opts.core) {
                core_size_ = *opts.core;
            }
            if (opts.disk_usage) {
                disk_usage_ = *opts.disk_usage;
            }
        }

        auto cpu_time() const { return cpu_time_; }
        auto memory() const { return memory_usage_; }
        auto wall_time() const { return wall_time_; }
        auto extra_time() const { return extra_time_; }
        auto as_size() const { return as_size_bytes_; }
        auto stack_size() const { return stack_size_; }
        auto processes() const { return processes_; }
        auto disk_usage() const { return disk_usage_; }
        auto open_files() const { return open_files_; }
        auto file_size() const { return file_size_; }
        auto core_dump_size() const { return core_size_; }

        void set_cpu_time(size_t s) { cpu_time_ = s; }
        void set_memory(size_t bytes) { memory_usage_ = bytes; }
        void set_wall_time(size_t s) { wall_time_ = s; }
        void set_extra_time(size_t s) { extra_time_ = s; }
        void set_as_size(size_t b) { as_size_bytes_ = b; }
        void set_stack_size(size_t b) { stack_size_ = b; }
        void set_processes(size_t n) { processes_ = n; }
        void set_open_files(size_t n) { open_files_ = n; }
        void set_file_size(size_t b) { file_size_ = b; }
        void set_core_dump_size(size_t b) { core_size_ = b; }

       private:
        /// @brief Maximum CPU time in seconds.
        std::optional<size_t> cpu_time_;

        /// @brief Extra time, added to cpu_time_s_, in seconds.
        std::optional<size_t> extra_time_;

        /// @brief Maximum total memory utilization in bytes.
        std::optional<size_t> memory_usage_;

        /// @brief Maximum address space size in bytes.
        std::optional<size_t> as_size_bytes_;

        /// @brief Maximum stack size in bytes.
        std::optional<size_t> stack_size_;

        /// @brief Maximum number of child processes and threads.
        std::optional<size_t> processes_;

        /// @brief Maximum total size of files on the disk.
        std::optional<size_t> disk_usage_;

        /// @brief Limit on the number of simultaneously opened file
        /// descriptors.
        std::optional<size_t> open_files_;

        /// @brief Maximum size of files.
        std::optional<size_t> file_size_;

        /// @brief Maximum size of a core dump file that is created when
        /// isolated program crashes. Longer dumps get truncated to this size.
        std::optional<size_t> core_size_ = 0;

        /// @brief Maximum wall time.
        size_t wall_time_ = DEFAULT_WALL_TIME;

        static constexpr size_t DEFAULT_WALL_TIME = 60;

        void set_defaults() {}
    };

    /// @brief Internal representation of metadata about the run of the proxy.
    struct proxy_stats {};

    enum class exit_status {
        OK,                     ///< Task exited normally with exit code 0.
        WALL_TIME_EXCEEDED,     ///< Task exceeded the wall time limit.
        CPU_TIME_EXCEEDED,      ///< Task exceeded the CPU time limit.
        MEMORY_LIMIT_EXCEEDED,  ///< Task exceeded the memory limit.
        KILLED,                 ///< Task was killed by a signal.
        NON_ZERO_EXIT_CODE      ///< Task exited with a non-zero exit code.
    };

    /// @brief Internal representation of metadata about the run of a single
    /// task.
    struct task_stats {
        task_config* config_;
        bool exited_normally;
        bool signalled;
        int exit_code;
        int err_no;
        int signal;

        exit_status exit;

        /// @brief Memory usage in bytes from cgroups accounting.
        size_t cg_total_mem_bytes;

        /// @brief CPU time in microseconds from cgroups accounting.
        size_t cg_total_time_usec;

        size_t wall_time_ms;

        /// @brief Peak resident set size in KB from getrusage() (`ru_maxrss`,
        /// already KB on Linux). Emitted as Isolate's `max-rss`; the honest
        /// memory signal when cgroup `cg-mem` is unmeasurable (ADR 0006).
        size_t rusage_max_rss_kb;

        /// @brief CPU time in microseconds from getrusage().
        long rusage_total_time_usec;

        /// @brief Voluntary / involuntary context switches from getrusage()
        /// (`ru_nvcsw` / `ru_nivcsw`). Isolate's `csw-voluntary` / `csw-forced`.
        size_t csw_voluntary;
        size_t csw_forced;

        /// @brief Whether cgroup peak memory (`memory.peak`) could be read. When
        /// false (e.g. el9 / kernel 5.14 without the backport) the `cg-mem` line
        /// is omitted from the meta-file rather than reported as 0 (ADR 0006).
        bool cg_mem_measured;
    };

    /// @brief not implemented.
    class task_report {
       public:
        void insert(task_stats task) { tasks_.emplace_back(task); }

        const auto& tasks() const { return tasks_; }

       private:
        std::vector<task_stats> tasks_;
    };

    /// @brief Configuration class for a single task.
    class task_config {
       public:
        task_config(const YAML::Node& task_node) {
            if (!task_node) {
                terminate("Invalid task node");
            }

            if (!task_node[config_options::task::TASK_ID]) {
                terminate("Missing task id");
            }

            if (!task_node[config_options::task::CMD] ||
                !task_node[config_options::task::CMD]
                          [config_options::task::EXEC_PATH]) {
                terminate("Missing path to executable for task \"{}\"", id_);
            }

            id_ = task_node[config_options::task::TASK_ID].as<std::string>();
            exec_ = fs::path(task_node[config_options::task::CMD]
                                      [config_options::task::EXEC_PATH]
                                          .as<std::string>());
            rlimits_ = resource_limits(task_node[config_options::task::RLIMS]);

            if (task_node[config_options::task::CMD]
                         [config_options::task::EXEC_ARGS]) {
                args_ = yaml_utils::get_vector<std::string>(
                    task_node[config_options::task::CMD]
                             [config_options::task::EXEC_ARGS]);
            }
            args_.insert(args_.begin(), exec_.string());

            if (task_node[config_options::STATS_YAML]) {
                results_file_ =
                    task_node[config_options::STATS_YAML].as<std::string>();
            }

            if (task_node[config_options::task::STDIN_FILE]) {
                stdin_file_ =
                    fs::path(task_node[config_options::task::STDIN_FILE]
                                 .as<std::string>());
            }

            if (task_node[config_options::task::STDOUT_FILE]) {
                stdout_file_ =
                    fs::path(task_node[config_options::task::STDOUT_FILE]
                                 .as<std::string>());
            }

            if (task_node[config_options::task::STDERR_FILE]) {
                stderr_file_ =
                    fs::path(task_node[config_options::task::STDERR_FILE]
                                 .as<std::string>());
            }

            if (task_node[config_options::task::STDERR_TO_STDOUT]) {
                stderr_to_stdout_ =
                    task_node[config_options::task::STDERR_TO_STDOUT]
                        .as<bool>();
            }

            if (task_node[config_options::task::CHDIR]) {
                chdir_ = fs::path(
                    task_node[config_options::task::CHDIR].as<std::string>());
            }
        }

        /// @brief Construct the single task of a compatibility-mode `--run`
        /// from the flat CLI options. The program and its arguments come from
        /// the positionals after `--`; there is no stats-yaml in compat mode
        /// (the meta-file is written by the root process, ADR 0005).
        /// @details The task is named "task" — a fixed string, because the name
        /// becomes the task's cgroup directory (task_supervisor) and must not
        /// be caller-controlled; it is trivially unique since compat mode runs
        /// exactly one task. The working directory defaults to `/box` like
        /// Isolate's (`--chdir` overrides, relative to the sandbox root), so
        /// relative program/stdio paths resolve in the Worker's staging dir.
        task_config(const cli::cli_options& opts) : rlimits_(opts) {
            if (opts.program.empty()) {
                terminate("No path to executable provided");
            }

            id_ = "task";
            exec_ = fs::path(opts.program);
            args_ = opts.args;
            args_.insert(args_.begin(), exec_.string());

            if (opts.stdin_file) {
                stdin_file_ = fs::path(*opts.stdin_file);
            }

            if (opts.stdout_file) {
                stdout_file_ = fs::path(*opts.stdout_file);
            }

            if (opts.stderr_file) {
                stderr_file_ = fs::path(*opts.stderr_file);
            }

            chdir_ = opts.chdir ? fs::path(*opts.chdir) : fs::path("/box");

            stderr_to_stdout_ = opts.stderr_to_stdout;
        }

        auto& exec_args() { return args_; }
        const auto& name() const { return id_; }
        const auto& exec_path() const { return exec_; }
        const auto& stdin_file() const { return stdin_file_; }
        const auto& stdout_file() const { return stdout_file_; }
        const auto& stderr_file() const { return stderr_file_; }
        const auto& stderr_to_stdout() const { return stderr_to_stdout_; }
        const auto& chdir() const { return chdir_; }
        const auto& rlimits() const { return rlimits_; }
        const auto& stats_path() const { return results_file_; }

        /// @brief
        /// @param stats
        void finalize_task(const task_stats& stats) {
            task_stats_ = stats;
            if (results_file_.has_value()) {
                generate_stats_yaml(results_file_.value(), task_stats_.value());
            }
        }

       private:
        /// @brief Task name unique within a single box.
        std::string id_;

        /// @brief Path to the executable inside the box.
        fs::path exec_;

        /// @brief Arguments for the executable.
        std::vector<std::string> args_;

        /// @brief Resource limits for this task.
        resource_limits rlimits_;

        /// @brief Optional file to redirect stdin from, has to be accesible
        /// inside the box. If not specified, standard input is transitively
        /// inherited from the root process.
        std::optional<fs::path> stdin_file_;

        /// @brief Optional file to redirect stdout to. Path is relative to the
        /// box root. If not specified, standard output is transitively
        /// inherited from the root process.
        std::optional<fs::path> stdout_file_;

        /// @brief Optional file to redirect stderr to. Path is relative to the
        /// box root. If not specified, stderr is transitively inherited from
        /// the root process.
        std::optional<fs::path> stderr_file_;

        /// @brief Redirect stderr to stdout. Performed after stdout is
        /// redirected to stdout_file_ if specified.
        bool stderr_to_stdout_ = false;

        /// @brief Optional directory inside box to chdir() to before execve().
        std::optional<fs::path> chdir_;

        /// @brief Path of generated results file.
        std::optional<fs::path> results_file_;

        /// @brief
        std::optional<task_stats> task_stats_;

        /// @brief Generate a yaml results file.
        /// @param path
        /// @param stats
        void generate_stats_yaml(const fs::path& path,
                                 const task_stats& stats) {
            logs::debug("Generating meta file for \"{}\" at sandbox path {}",
                        id_, path.string());
            YAML::Emitter yaml;
            yaml << YAML::BeginMap;
            yaml << YAML::Key << stats_names::STATUS;
            if (stats.exit == config::exit_status::WALL_TIME_EXCEEDED) {
                yaml << YAML::Value << stats_names::WALL_TIME_EXCEEDED;
            } else if (stats.exit == config::exit_status::CPU_TIME_EXCEEDED) {
                yaml << YAML::Value << stats_names::CPU_TIME_EXCEEDED;
            } else if (stats.exit ==
                       config::exit_status::MEMORY_LIMIT_EXCEEDED) {
                yaml << YAML::Value << stats_names::MEMORY_EXCEEDED;
            } else if (stats.signalled) {
                yaml << YAML::Value << stats_names::KILLED;
            } else if (stats.exit_code) {
                yaml << YAML::Value << stats_names::NON_ZERO_EXIT_CODE;
            } else if (stats.exited_normally && stats.exit_code == 0) {
                yaml << YAML::Value << stats_names::OK;
            }

            yaml << YAML::Key << stats_names::EXIT_CODE << YAML::Value
                 << stats.exit_code;
            yaml << YAML::Key << stats_names::SIGNAL << YAML::Value
                 << stats.signal;
            yaml << YAML::Key << stats_names::CG_TOTAL_TIME_S << YAML::Value
                 << (float)stats.cg_total_time_usec / (float)1000000;
            yaml << YAML::Key << stats_names::CG_TOTAL_MEM_KB << YAML::Value
                 << units::bytes_to_kib(stats.cg_total_mem_bytes);
            yaml << YAML::Key << stats_names::WALL_TIME_S << YAML::Value
                 << (float)stats.wall_time_ms / 1000;

            std::ofstream f(path);
            f << yaml.c_str();
        }
    };

    /// @brief Configuration class storing all tasks of a container run.
    class tasks_config {
       public:
        tasks_config() {}
        tasks_config(const YAML::Node& tasks_node) {
            if (!tasks_node || tasks_node.size() < 1) {
                logs::warn("No tasks specified, empty container run");
            } else {
                for (std::size_t i = 0; i < tasks_node.size(); i++) {
                    logs::debug("Adding task {}", i);
                    tasks_.emplace_back(
                        std::make_unique<task_config>(tasks_node[i]));
                }
            }
        }

        tasks_config(const cli::cli_options& opts) {
            tasks_.emplace_back(std::make_unique<task_config>(opts));
        }

        /// @brief Getter for task configurations.
        /// @return
        auto& get_tasks() const { return tasks_; }

       private:
        /// @brief Vector with task configurations.
        std::vector<std::unique_ptr<task_config>> tasks_;
    };

    /// @brief Internal representation of a directory rule. TODO: link to the
    /// documentation.
    class dir_rule {
       public:
        dir_rule(const std::string& rule) : rule_(rule) {
            construct_rule(rule);
        }

        const fs::path& in_dir() const { return inner_; }
        const fs::path& out_dir() const { return outer_; }
        const std::string& string() const { return rule_; }
        bool rw() const { return rw_; }
        bool dev() const { return dev_; }
        bool noexec() const { return noexec_; }
        bool maybe() const { return maybe_; }
        bool fs() const { return fs_; }
        bool tmp() const { return tmp_; }
        bool norec() const { return norec_; }
        bool allow_newdir() const { return allow_newdir_; }

       private:
        static constexpr auto rule_regex_ = "([^=:]+)(=([^:]+))?(:(.+))?";

        /// @brief The actual string with the rule, for error reporting.
        std::string rule_;

        /// @brief Path inside the box.
        fs::path inner_;

        /// @brief Outer path mounted inside the box.
        fs::path outer_;

        bool rw_ = false;      /// @brief Allow read/write to the directory.
        bool dev_ = false;     /// @brief TODO:
        bool noexec_ = false;  /// @brief Don't allow running executables from
                               /// this directory.
        bool maybe_ =
            false;  /// @brief Don't fail if the outer path doesn't exist.
        bool fs_ =
            false;  /// @brief Mount a filesystem, not a regular directory.
        bool tmp_ = false;  /// @brief Temporary directory used by the box that
                            /// will be deleted afterwards.
        bool norec_ = false;  /// @brief Disallow recursive mounting of
                              /// directories under outer_.
        bool allow_newdir_ =
            false;  /// @brief Allow creating a new directory for nested mounts.

        /// @brief
        /// @param rule
        /// @param box_root
        void construct_rule(const std::string& rule) {
            std::regex rule_regex(rule_regex_);
            std::smatch m;
            if (!std::regex_match(rule, m, rule_regex)) {
                terminate("Invalid fs-rule syntax: {}", rule);
            }

            auto& inner_token = m[1];
            auto& outer_token = m[3];
            auto& options_token = m[5];

            fs::path inner = fs::path(inner_token);

            std::optional<fs::path> outer;
            if (outer_token != "") {
                outer = std::optional<fs::path>(outer_token);
            }

            std::vector<std::string> options =
                string_utils::split(options_token);

            if (!check_inner_dir(inner)) {
                terminate("Invalid inner path in fs-rule: {}",
                          inner_token.str());
            }
            if (!check_outer_dir(outer)) {
                terminate("Invalid outer path in fs-rule: {}",
                          outer_token.str());
            }
            if (!check_options(options)) {
                terminate("Invalid options in fs-rule: {}",
                          options_token.str());
            }

            parse_options(options);
            inner_ = inner;
            outer_ = fs::path("/") / (outer ? outer.value() : inner);

            // logs::debug("Parsed fs-rule: inner={}, outer={}, rw={}, dev={},
            // noexec={}, maybe={}, fs={}, tmp={}, norec={}, allow_newdir={}",
            // inner_.string(), outer_.string(), rw_, dev_, noexec_, maybe_,
            // fs_, tmp_, norec_, allow_newdir_);
        }

        /// @brief
        /// @param options
        void parse_options(const std::vector<std::string>& options) {
            for (auto&& o : options) {
                if (o == "rw") {
                    rw_ = true;
                }
                if (o == "dev") {
                    dev_ = true;
                }
                if (o == "noexec") {
                    noexec_ = true;
                }
                if (o == "maybe") {
                    maybe_ = true;
                }
                if (o == "fs") {
                    fs_ = true;
                }
                if (o == "tmp") {
                    tmp_ = true;
                    rw_ = true;
                }
                if (o == "norec") {
                    norec_ = true;
                }
                if (o == "allow_newdir") {
                    allow_newdir_ = true;
                }
            }
        }

        /// @brief
        /// @param in
        /// @return
        static bool check_inner_dir(const fs::path& in) {
            return file_utils::is_valid_path(in) &&
                   file_utils::is_subdirectory(in);
        }

        /// @brief
        /// @param out
        /// @return
        static bool check_outer_dir(const std::optional<fs::path>& out) {
            return !out.has_value() || file_utils::is_valid_path(out.value());
        }

        /// @brief Not implemented yet.
        /// @param options
        /// @return
        static bool check_options(const std::vector<std::string>& options) {
            return true;
        }
    };

    /// @brief Configuration node for box_fs_manager.
    class box_fs_config {
       public:
        box_fs_config() {}

        /// @brief Compatibility-mode (`--run`) box filesystem: the default
        /// directory rules only, so the program can exec against the host's
        /// `/bin`, `/lib`, `/usr`, … Parsing `--dir` rules into user rules (and
        /// auditing the default set against Isolate's exact one) is issue #11.
        box_fs_config(const cli::cli_options& opts) { define_default_rules(); }

        /// @brief
        /// @param box_root
        /// @param env_node
        box_fs_config(const YAML::Node& box_fs_node) {
            define_default_rules();

            if (!box_fs_node) {
                _default();
            } else {
                if (box_fs_node
                        [config_options::box_fs::USE_DEFAULT_DIR_RULES]) {
                    use_defaults_ =
                        box_fs_node
                            [config_options::box_fs::USE_DEFAULT_DIR_RULES]
                                .as<bool>();
                }
                if (box_fs_node[config_options::box_fs::DIRECTORY_RULES]) {
                    add_rules(
                        box_fs_node[config_options::box_fs::DIRECTORY_RULES]);
                }
            }
        }

        const auto& rules() const { return rules_; }

        bool use_default_rules() const { return use_defaults_; }

        const auto& default_rules() const { return default_rules_; }

       private:
        /// @brief User defined directory rules.
        std::vector<dir_rule> rules_;

        /// @brief Apply default_rules_ before user defined ones.
        bool use_defaults_ = true;

        /// @brief Default directory rules, defined in add_default_rules().
        std::vector<dir_rule> default_rules_;

        void add_rules(const YAML::Node& rules_list) {
            for (std::size_t i = 0; i < rules_list.size(); i++) {
                auto new_rule = dir_rule(rules_list[i].as<std::string>());
                // Remove existing rule with same prefix if it exists
                rules_.erase(std::remove_if(rules_.begin(), rules_.end(),
                                            [&new_rule](const dir_rule& rule) {
                                                return rule.in_dir() ==
                                                       new_rule.in_dir();
                                            }),
                             rules_.end());

                rules_.emplace_back(std::move(new_rule));
            }
        }

        void define_default_rules() {
            // default_rules_.emplace_back(dir_rule("tmp:tmp"));
            default_rules_.emplace_back(dir_rule("etc"));
            default_rules_.emplace_back(dir_rule("bin"));
            default_rules_.emplace_back(dir_rule("dev:dev"));
            default_rules_.emplace_back(dir_rule("lib"));
            default_rules_.emplace_back(dir_rule("lib64:maybe"));
            default_rules_.emplace_back(dir_rule("proc=proc:fs"));
            default_rules_.emplace_back(dir_rule("usr"));
        }

        void _default() {}
    };

    /// @brief Internal representation of an environment rule (used by
    /// env_manager).
    class env_rule {
       public:
        env_rule(const std::string& rule) {
            if (rule.substr(0, 9) == "full-env=") {
                std::string val = rule.substr(9);
                if (val == "true") {
                    full_env_ = true;
                } else if (val != "false") {
                    terminate("Invalid value in environment rule ({})", rule);
                }
            } else {
                auto eq = rule.find('=');
                if (eq == std::string::npos) {
                    inherit_ = rule;
                } else {
                    std::string name = rule.substr(0, eq);
                    std::string value = rule.substr(eq + 1);
                    name_value_pair_ = std::tuple(name, value);
                }
            }
        }

        const auto& inherited_var() const { return inherit_; }

        const auto& name_value_pair() const { return name_value_pair_; }

        bool full_env() const { return full_env_; }

       private:
        bool full_env_ = false;
        std::optional<std::string> inherit_;
        std::optional<std::tuple<std::string, std::string>> name_value_pair_;
    };

    /// @brief Configuration node for env_manager.
    class env_config {
       public:
        env_config() {}

        env_config(const YAML::Node& env_node) {
            if (!env_node) {
                _default();
            } else {
                if (env_node[config_options::env::INHERIT_ALL]) {
                    inherit_all_ =
                        env_node[config_options::env::INHERIT_ALL].as<bool>();
                }
                if (env_node[config_options::env::ENV_VARS]) {
                    auto rules_list = env_node[config_options::env::ENV_VARS];
                    rules_ = parse_rules(rules_list);
                }
            }
        }

        env_config(const cli::cli_options& opts) {
            for (const auto& rule_str : opts.env_rules) {
                env_rule rule(rule_str);
                if (rule.full_env()) {
                    inherit_all_ = true;
                } else {
                    rules_.emplace_back(std::move(rule));
                }
            }
        }

        void _default() {}

        const auto& rules() const { return rules_; }

        const auto& inherit_all() const { return inherit_all_; }

       private:
        std::vector<env_rule> rules_;
        bool inherit_all_ = false;

        std::vector<env_rule> parse_rules(const YAML::Node& rules_list) {
            std::vector<env_rule> rules;
            for (std::size_t i = 0; i < rules_list.size(); i++) {
                auto rule = env_rule(rules_list[i].as<std::string>());
                if (rule.full_env()) {
                    inherit_all_ = true;
                } else {
                    rules.emplace_back(rule);
                }
            }
            return rules;
        }
    };

    /// @brief Configuration node for the credential_manager classes.
    class credentials_config {
       public:
        credentials_config() {}
        credentials_config(const YAML::Node& credentials_node) {
            if (!credentials_node) {
                _default();
            } else {
                if (credentials_node[config_options::BOXES_DIR]) {
                    boxes_dir_ =
                        fs::path(credentials_node[config_options::BOXES_DIR]
                                     .as<std::string>());
                }
                if (credentials_node[config_options::BOXES_CGROUP]) {
                    boxes_cgroup_ =
                        fs::path(credentials_node[config_options::BOXES_CGROUP]
                                     .as<std::string>());
                }
                if (credentials_node
                        [config_options::credentials::INSTANCE_NAME]) {
                    instance_name_ =
                        credentials_node
                            [config_options::credentials::INSTANCE_NAME]
                                .as<std::string>();
                }
                if (credentials_node
                        [config_options::credentials::INSTANCE_ID]) {
                    instance_id_ =
                        credentials_node
                            [config_options::credentials::INSTANCE_ID]
                                .as<size_t>();
                }
                if (credentials_node[config_options::credentials::BOX_UID]) {
                    box_uid_ =
                        credentials_node[config_options::credentials::BOX_UID]
                            .as<size_t>();
                }
                if (credentials_node[config_options::credentials::BOX_GID]) {
                    box_gid_ =
                        credentials_node[config_options::credentials::BOX_GID]
                            .as<size_t>();
                }
            }
        }

        /// @brief Construct from the flat CLI options. In compatibility mode
        /// the only caller-supplied identity is `--box-id`; the box UID/GID and
        /// the boxes_dir/cgroup paths are derived from it by the credentials
        /// manager (ADR 0001).
        credentials_config(const cli::cli_options& opts) {
            if (opts.box_id) {
                instance_id_ = *opts.box_id;
            }
        }

        const auto& instance_name() const { return instance_name_; }
        const auto& instance_id() const { return instance_id_; }

        /// @brief Return the path to the common directory for all container
        /// instances.
        const fs::path& boxes_dir() const { return boxes_dir_; }

        /// @brief Return the path to the common cgroup for all container
        /// instances.
        const fs::path& boxes_cgroup() const { return boxes_cgroup_; }

        const auto& box_uid() const { return box_uid_; }

        const auto& box_gid() const { return box_gid_; }

       private:
        fs::path boxes_dir_ = defaults::BOXES_DIR;
        fs::path boxes_cgroup_ = defaults::BOXES_CGROUP;

        std::optional<std::string> instance_name_;
        std::optional<size_t> instance_id_;
        std::optional<size_t> box_uid_;
        std::optional<size_t> box_gid_;

        void _default() {}
    };

    /// @brief Configuration node for the proxy_core.
    class proxy_config {
       public:
        proxy_config() {}
        proxy_config(const YAML::Node& proxy_node) {
            if (!proxy_node) {
                terminate("No proxy node specified in configuration file!");
            } else {
                tasks_ = tasks_config(proxy_node[config_options::TASKS]);
                env_ = env_config(proxy_node[config_options::ENV]);
                box_fs_ = box_fs_config(proxy_node[config_options::BOX_FS]);
                if (proxy_node[config_options::SHARE_NET]) {
                    share_net_ =
                        proxy_node[config_options::SHARE_NET].as<bool>();
                } else {
                    logs::debug(
                        "No share_net option specified, defaulting to false");
                }
            }
        }

        proxy_config(const cli::cli_options& opts) {
            tasks_ = tasks_config(opts);
            env_ = env_config(opts);
            box_fs_ = box_fs_config(opts);  // defaults only; --dir is issue #11
            share_net_ = opts.share_net;
        }

        const auto& get_tasks_config() const { return tasks_; }

        const auto& fs_config() const { return box_fs_; }

        const auto& get_env_config() const { return env_; }

        const auto& box_root() const { return box_root_; }

        bool share_net() const { return share_net_; }

       private:
        fs::path box_root_;
        tasks_config tasks_;
        env_config env_;
        box_fs_config box_fs_;

        /// @brief Launch the proxy and tasks in the same network namespace as
        /// the root process.
        bool share_net_ = false;

        void _default() { tasks_ = tasks_config(YAML::Node()); }
    };

    /// @brief Root node storing the configuration hierarchy.
    class root_configuration {
       public:
        root_configuration(int argc, char** argv) { parse_options(argc, argv); }

        auto& get_proxy_config() const { return proxy_config_; }

        auto& get_credentials_config() const { return creds_config_; }

        /// @brief The lifecycle phase selected on the command line. `root_core`
        /// dispatches on this to pick standalone / --init / --run / --cleanup.
        cli::run_mode mode() const { return mode_; }

        /// @brief Host path for the Isolate-format meta-file (`--meta`), if the
        /// caller supplied one. Compat-mode only; the root process is its sole
        /// writer (ADR 0005). Absent ⇒ no meta-file is written.
        const std::optional<fs::path>& meta() const { return meta_; }

       private:
        cli::run_mode mode_ = cli::run_mode::none;
        credentials_config creds_config_;
        proxy_config proxy_config_;
        std::optional<fs::path> meta_;

        void parse_options(int argc, char** argv) {
            cli::cli_options opts = cli::parse(argc, argv);
            mode_ = opts.mode;

            if (opts.debug) {
                logs::set_level(logs::level::debug);
                logs::debug("Debug output enabled");
            } else {
                logs::set_level(logs::level::critical);
            }

            if (mode_ == cli::run_mode::standalone) {
                auto f = fs::path(opts.yaml.value());
                try {
                    logs::debug("Reading configuration from file: {}",
                                f.string());
                    configure_from_yaml(f);
                } catch (YAML::BadFile&) {
                    terminate("Bad configuration file: {}", f.string());
                }
            } else {
                // Compatibility three-phase mode (--init/--run/--cleanup). All
                // three phases derive their box identity from --box-id, so
                // creds_config_ is always built. Only --run carries a program to
                // run, so proxy_config_ (which builds a task from the positional
                // program) is built for --run alone: building it for --init /
                // --cleanup would hit task_config's empty-program terminate().
                creds_config_ = credentials_config(opts);
                if (opts.meta) {
                    meta_ = fs::path(*opts.meta);
                }
                if (mode_ == cli::run_mode::run) {
                    proxy_config_ = proxy_config(opts);
                }
            }
        }

        void configure_from_yaml(const fs::path& f) {
            YAML::Node config = YAML::LoadFile(f);
            proxy_config_ = proxy_config(config);
            creds_config_ = credentials_config(config);
        }
    };
}  // namespace config

#endif
