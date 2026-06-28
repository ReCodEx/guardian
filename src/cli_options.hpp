#ifndef CLI_OPTIONS
#define CLI_OPTIONS

#include <getopt.h>

#include <charconv>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "logs.hpp"

/// @brief Hand-rolled, library-agnostic command-line front-end.
/// @details Replaces Boost.program_options (ADR 0002): a single flat
/// @ref cli::cli_options struct, produced by @ref cli::parse and consumed by
/// the config-class constructors. Maps Isolate's compatibility flag names
/// (`--cg-mem`, `--time`, …) onto semantic fields, accepts-and-ignores the
/// cosmetic `--cg` / `--cg-timing`, and rejects unknown flags with exit code 2
namespace cli {

    /// @brief Operating mode selected by the mutually-exclusive phase flags.
    enum class run_mode {
        none,        ///< No mode flag seen (usage error).
        init,        ///< `--init`: create the box directory tree.
        run,         ///< `--run -- prog args`: run a program in the box.
        cleanup,     ///< `--cleanup`: remove the box + cgroup.
        standalone,  ///< `--yaml=<file>`: single-shot YAML mode.
    };

    /// @brief Flat representation of every command-line option.
    /// @details `std::optional` marks "flag absent" where presence is
    /// meaningful; plain members carry their own defaults.
    struct cli_options {
        run_mode mode = run_mode::none;

        /// @brief Enable debug-level logging (`--debug`).
        bool debug = false;

        // --- box identity / compatibility ---

        /// @brief `--box-id=<N>`; the per-box key shared across the three
        /// phases.
        std::optional<std::size_t> box_id;

        /// @brief `--quota=<blocks>,<inodes>` (accepted; enforcement deferred).
        std::optional<std::string> quota;

        /// @brief `--meta=<host path>` for the Isolate-format meta-file.
        std::optional<std::string> meta;

        // --- resource limits (semantic names; compat flags mapped onto them)
        // ---

        std::optional<std::size_t> memory;      ///< `--cg-mem` / `--mem`.
        std::optional<std::size_t> as_size;     ///< `--as-size`.
        std::optional<std::size_t> cpu_time;    ///< `--time`.
        std::optional<std::size_t> wall_time;   ///< `--wall-time`.
        std::optional<std::size_t> extra_time;  ///< `--extra-time`.
        std::optional<std::size_t> stack;       ///< `--stack`.
        std::optional<std::size_t> file_size;   ///< `--fsize`.
        std::optional<std::size_t> open_files;  ///< `--open-files`.
        std::optional<std::size_t> core;        ///< `--core`.
        std::optional<std::size_t> disk_usage;  ///< `--disk-usage`.

        /// @brief `--processes[=N]`: absent = nullopt, bare = 0 (unlimited),
        /// else the limit.
        std::optional<std::size_t> processes;

        // --- task I/O ---

        std::optional<std::string> stdin_file;   ///< `--stdin`.
        std::optional<std::string> stdout_file;  ///< `--stdout`.
        std::optional<std::string> stderr_file;  ///< `--stderr`.
        bool stderr_to_stdout = false;           ///< `--stderr-to-stdout`.
        std::optional<std::string> chdir;        ///< `--chdir`.

        // --- environment & filesystem rules (repeatable) ---

        std::vector<std::string> env_rules;  ///< `--env=NAME[=VALUE]`.
        std::vector<std::string> dir_rules;  ///< `--dir=...`.

        bool share_net = false;  ///< `--share-net`.

        // --- standalone YAML mode ---

        std::optional<std::string> yaml;  ///< `--yaml=<file>`.

        // --- program + args following `--` (run mode) ---

        std::string program;
        std::vector<std::string> args;
    };

    namespace detail {

        /// @brief Log a usage error and exit with Isolate's "internal error"
        /// code (2).
        template <typename... Args>
        [[noreturn]] inline void usage_error(std::format_string<Args...> fmt,
                                             Args&&... args) {
            logs::critical(fmt, std::forward<Args>(args)...);
            exit(2);
        }

        /// @brief Parse a non-negative integer option value, exiting 2 on
        /// garbage.
        inline std::size_t parse_size(std::string_view flag, const char* arg) {
            std::size_t value = 0;
            const char* end = arg + std::strlen(arg);
            auto [ptr, ec] = std::from_chars(arg, end, value);
            if (ec != std::errc{} || ptr != end) {
                usage_error("Invalid numeric value for --{}: {}", flag, arg);
            }
            return value;
        }

        /// @brief Set @p mode to @p next, refusing a second conflicting phase
        /// flag.
        inline void select_mode(run_mode& mode, run_mode next,
                                std::string_view flag) {
            if (mode != run_mode::none && mode != next) {
                usage_error("Conflicting mode flag --{}", flag);
            }
            mode = next;
        }

        /// @brief Enforce the cross-field invariants the parser cannot express
        /// per-flag.
        inline void validate(const cli_options& opts) {
            if (opts.mode == run_mode::none) {
                usage_error(
                    "No mode selected (expected one of --init/--run/--cleanup "
                    "or --yaml=<file>)");
            }

            const bool compat = opts.mode == run_mode::init ||
                                opts.mode == run_mode::run ||
                                opts.mode == run_mode::cleanup;
            if (compat && !opts.box_id.has_value()) {
                usage_error("--box-id is required for --init/--run/--cleanup");
            }

            if (opts.mode == run_mode::run && opts.program.empty()) {
                usage_error("--run requires a program after `--`");
            }
            if (opts.mode != run_mode::run && !opts.program.empty()) {
                usage_error("Program arguments are only valid with --run");
            }
        }

        /// @brief Stable integer ids for each long option (no short options
        /// exist).
        enum opt_id {
            OPT_INIT = 256,
            OPT_RUN,
            OPT_CLEANUP,
            OPT_DEBUG,
            OPT_CG,
            OPT_CG_TIMING,
            OPT_BOX_ID,
            OPT_QUOTA,
            OPT_META,
            OPT_CG_MEM,
            OPT_MEM,
            OPT_AS_SIZE,
            OPT_TIME,
            OPT_WALL_TIME,
            OPT_EXTRA_TIME,
            OPT_STACK,
            OPT_FSIZE,
            OPT_OPEN_FILES,
            OPT_CORE,
            OPT_DISK_USAGE,
            OPT_PROCESSES,
            OPT_STDIN,
            OPT_STDOUT,
            OPT_STDERR,
            OPT_STDERR_TO_STDOUT,
            OPT_CHDIR,
            OPT_ENV,
            OPT_DIR,
            OPT_SHARE_NET,
            OPT_YAML,
        };

    }  // namespace detail

    /// @brief Parse @p argv into a @ref cli_options.
    /// @details On any usage error (unknown flag, missing/garbage value,
    /// conflicting modes) logs to stderr and `exit(2)`. The program and its
    /// arguments for `--run` follow a `--` separator and are taken from the
    /// remaining positional arguments.
    inline cli_options parse(int argc, char** argv) {
        using namespace detail;

        static const struct option long_opts[] = {
            {.name="init",             .has_arg=no_argument,       .flag=nullptr, .val=OPT_INIT},
            {.name="run",              .has_arg=no_argument,       .flag=nullptr, .val=OPT_RUN},
            {.name="cleanup",          .has_arg=no_argument,       .flag=nullptr, .val=OPT_CLEANUP},
            {.name="debug",            .has_arg=no_argument,       .flag=nullptr, .val=OPT_DEBUG},
            {.name="cg",               .has_arg=no_argument,       .flag=nullptr, .val=OPT_CG},
            {.name="cg-timing",        .has_arg=no_argument,       .flag=nullptr, .val=OPT_CG_TIMING},
            {.name="box-id",           .has_arg=required_argument, .flag=nullptr, .val=OPT_BOX_ID},
            {.name="quota",            .has_arg=required_argument, .flag=nullptr, .val=OPT_QUOTA},
            {.name="meta",             .has_arg=required_argument, .flag=nullptr, .val=OPT_META},
            {.name="cg-mem",           .has_arg=required_argument, .flag=nullptr, .val=OPT_CG_MEM},
            {.name="mem",              .has_arg=required_argument, .flag=nullptr, .val=OPT_MEM},
            {.name="as-size",          .has_arg=required_argument, .flag=nullptr, .val=OPT_AS_SIZE},
            {.name="time",             .has_arg=required_argument, .flag=nullptr, .val=OPT_TIME},
            {.name="wall-time",        .has_arg=required_argument, .flag=nullptr, .val=OPT_WALL_TIME},
            {.name="extra-time",       .has_arg=required_argument, .flag=nullptr, .val=OPT_EXTRA_TIME},
            {.name="stack",            .has_arg=required_argument, .flag=nullptr, .val=OPT_STACK},
            {.name="fsize",            .has_arg=required_argument, .flag=nullptr, .val=OPT_FSIZE},
            {.name="open-files",       .has_arg=required_argument, .flag=nullptr, .val=OPT_OPEN_FILES},
            {.name="core",             .has_arg=required_argument, .flag=nullptr, .val=OPT_CORE},
            {.name="disk-usage",       .has_arg=required_argument, .flag=nullptr, .val=OPT_DISK_USAGE},
            {.name="processes",        .has_arg=optional_argument, .flag=nullptr, .val=OPT_PROCESSES},
            {.name="stdin",            .has_arg=required_argument, .flag=nullptr, .val=OPT_STDIN},
            {.name="stdout",           .has_arg=required_argument, .flag=nullptr, .val=OPT_STDOUT},
            {.name="stderr",           .has_arg=required_argument, .flag=nullptr, .val=OPT_STDERR},
            {.name="stderr-to-stdout", .has_arg=no_argument,       .flag=nullptr, .val=OPT_STDERR_TO_STDOUT},
            {.name="chdir",            .has_arg=required_argument, .flag=nullptr, .val=OPT_CHDIR},
            {.name="env",              .has_arg=required_argument, .flag=nullptr, .val=OPT_ENV},
            {.name="dir",              .has_arg=required_argument, .flag=nullptr, .val=OPT_DIR},
            {.name="share-net",        .has_arg=no_argument,       .flag=nullptr, .val=OPT_SHARE_NET},
            {.name="yaml",             .has_arg=required_argument, .flag=nullptr, .val=OPT_YAML},
            {.name=nullptr,            .has_arg=0,                 .flag=nullptr, .val=0},
        };

        cli_options opts;

        // Leading ':' distinguishes a missing required argument (':') from an
        // unknown option ('?'); silence getopt's own messages, we print ours.
        opterr = 0;
        // `optind = 0` (not 1) is glibc's full reset: it reinitializes getopt's
        // internal scan state so repeated parses in one process (the unit tests)
        // start clean. For production's single call the behavior is identical
        // (glibc sets optind to 1 and scans from argv[1]).
        optind = 0;

        int id = 0;
        while ((id = getopt_long(argc, argv, ":", long_opts, nullptr)) != -1) {
            switch (id) {
                case OPT_INIT:
                    select_mode(opts.mode, run_mode::init, "init");
                    break;
                case OPT_RUN:
                    select_mode(opts.mode, run_mode::run, "run");
                    break;
                case OPT_CLEANUP:
                    select_mode(opts.mode, run_mode::cleanup, "cleanup");
                    break;
                case OPT_DEBUG:
                    opts.debug = true;
                    break;

                // Cosmetic flags accepted for compatibility, then ignored.
                case OPT_CG:
                case OPT_CG_TIMING:
                    break;

                case OPT_BOX_ID:
                    opts.box_id = parse_size("box-id", optarg);
                    break;
                case OPT_QUOTA:
                    opts.quota = optarg;
                    break;
                case OPT_META:
                    opts.meta = optarg;
                    break;

                case OPT_CG_MEM:
                    opts.memory = parse_size("cg-mem", optarg);
                    break;
                case OPT_MEM:
                    opts.memory = parse_size("mem", optarg);
                    break;
                case OPT_AS_SIZE:
                    opts.as_size = parse_size("as-size", optarg);
                    break;
                case OPT_TIME:
                    opts.cpu_time = parse_size("time", optarg);
                    break;
                case OPT_WALL_TIME:
                    opts.wall_time = parse_size("wall-time", optarg);
                    break;
                case OPT_EXTRA_TIME:
                    opts.extra_time = parse_size("extra-time", optarg);
                    break;
                case OPT_STACK:
                    opts.stack = parse_size("stack", optarg);
                    break;
                case OPT_FSIZE:
                    opts.file_size = parse_size("fsize", optarg);
                    break;
                case OPT_OPEN_FILES:
                    opts.open_files = parse_size("open-files", optarg);
                    break;
                case OPT_CORE:
                    opts.core = parse_size("core", optarg);
                    break;
                case OPT_DISK_USAGE:
                    opts.disk_usage = parse_size("disk-usage", optarg);
                    break;
                case OPT_PROCESSES:
                    // Bare `--processes` (optarg == nullptr) means unlimited.
                    opts.processes =
                        optarg ? parse_size("processes", optarg) : 0;
                    break;

                case OPT_STDIN:
                    opts.stdin_file = optarg;
                    break;
                case OPT_STDOUT:
                    opts.stdout_file = optarg;
                    break;
                case OPT_STDERR:
                    opts.stderr_file = optarg;
                    break;
                case OPT_STDERR_TO_STDOUT:
                    opts.stderr_to_stdout = true;
                    break;
                case OPT_CHDIR:
                    opts.chdir = optarg;
                    break;

                case OPT_ENV:
                    opts.env_rules.emplace_back(optarg);
                    break;
                case OPT_DIR:
                    opts.dir_rules.emplace_back(optarg);
                    break;
                case OPT_SHARE_NET:
                    opts.share_net = true;
                    break;

                case OPT_YAML:
                    select_mode(opts.mode, run_mode::standalone, "yaml");
                    opts.yaml = optarg;
                    break;

                case ':':
                    usage_error("Missing value for option {}",
                                argv[optind - 1]);
                case '?':
                default:
                    usage_error("Unknown option {}", argv[optind - 1]);
            }
        }

        // Everything after `--` is the program to run plus its arguments.
        if (optind < argc) {
            opts.program = argv[optind++];
            for (int i = optind; i < argc; i++) {
                opts.args.emplace_back(argv[i]);
            }
        }

        validate(opts);
        return opts;
    }

}  // namespace cli

#endif
