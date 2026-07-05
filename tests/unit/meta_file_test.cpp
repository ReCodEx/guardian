// Unit tests for meta::write_result — the compatibility-mode Isolate meta-file
// writer (slice 1: the metric superset). Exercised host-side with no root: a
// hand-built task_stats is written to a temp path and the exact emitted text is
// asserted, key-for-key against the Isolate contract (units, order, and the
// always-emit rule with cg-mem as the sole omit-when-unmeasurable exception).

#include "meta_file.hpp"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace {

    namespace fs = std::filesystem;

    class MetaFileTest : public ::testing::Test {
       protected:
        fs::path path_;

        void SetUp() override {
            char tmpl[] = "/tmp/meta_file_test_XXXXXX";
            int fd = ::mkstemp(tmpl);
            ASSERT_GE(fd, 0);
            ::close(fd);
            path_ = tmpl;
        }

        void TearDown() override {
            std::error_code ec;
            fs::remove(path_, ec);
        }

        std::string read_back() const {
            std::ifstream f(path_, std::ios::binary);
            std::ostringstream ss;
            ss << f.rdbuf();
            return ss.str();
        }

        // A completed task with representative metric values:
        //   cpu 1.234s, wall 2.500s, max-rss 4096 KB, 10/3 ctx switches,
        //   cg peak 2 MiB (2048 KB), measured.
        static config::task_stats baseline() {
            return config::task_stats{
                .config_ = nullptr,
                .exited_normally = true,
                .signalled = false,
                .exit_code = 0,
                .err_no = 0,
                .signal = 0,
                .exit = config::exit_status::OK,
                .cg_total_mem_bytes = 2u * 1024 * 1024,
                .cg_total_time_usec = 1'234'000,
                .wall_time_ms = 2500,
                .rusage_max_rss_kb = 4096,
                .rusage_total_time_usec = 1'200'000,
                .csw_voluntary = 10,
                .csw_forced = 3,
                .cg_mem_measured = true,
            };
        }
    };

    // OK: no status line (Isolate omits status on success), exitcode present,
    // full metric superset in Isolate emission order, cg-mem measured.
    TEST_F(MetaFileTest, ok_emits_full_superset_without_status) {
        meta::write_result(path_, baseline());
        EXPECT_EQ(read_back(),
                  "exitcode:0\n"
                  "time:1.234\n"
                  "time-wall:2.500\n"
                  "max-rss:4096\n"
                  "csw-voluntary:10\n"
                  "csw-forced:3\n"
                  "cg-mem:2048\n");
    }

    // Non-zero normal exit -> RE, exitcode carries the code.
    TEST_F(MetaFileTest, nonzero_exit_is_RE) {
        auto s = baseline();
        s.exit_code = 42;
        meta::write_result(path_, s);
        EXPECT_EQ(read_back(),
                  "status:RE\n"
                  "exitcode:42\n"
                  "time:1.234\n"
                  "time-wall:2.500\n"
                  "max-rss:4096\n"
                  "csw-voluntary:10\n"
                  "csw-forced:3\n"
                  "cg-mem:2048\n");
    }

    // Killed by a signal -> SG, exitsig (no exitcode: did not exit normally).
    TEST_F(MetaFileTest, signal_is_SG) {
        auto s = baseline();
        s.exited_normally = false;
        s.signalled = true;
        s.signal = 11;
        meta::write_result(path_, s);
        EXPECT_EQ(read_back(),
                  "status:SG\n"
                  "exitsig:11\n"
                  "time:1.234\n"
                  "time-wall:2.500\n"
                  "max-rss:4096\n"
                  "csw-voluntary:10\n"
                  "csw-forced:3\n"
                  "cg-mem:2048\n");
    }

    // Wall-time overrun -> TO (killed:1 discriminator is deferred to slice 3).
    TEST_F(MetaFileTest, wall_time_exceeded_is_TO) {
        auto s = baseline();
        s.exited_normally = false;
        s.signalled = true;
        s.signal = 9;
        s.exit = config::exit_status::WALL_TIME_EXCEEDED;
        meta::write_result(path_, s);
        EXPECT_EQ(read_back(),
                  "status:TO\n"
                  "exitsig:9\n"
                  "time:1.234\n"
                  "time-wall:2.500\n"
                  "max-rss:4096\n"
                  "csw-voluntary:10\n"
                  "csw-forced:3\n"
                  "cg-mem:2048\n");
    }

    // memory.peak unavailable: cg-mem line is omitted entirely (ADR 0006); every
    // other key stays present, max-rss carries the memory signal.
    TEST_F(MetaFileTest, unmeasurable_cg_mem_omits_the_line) {
        auto s = baseline();
        s.cg_mem_measured = false;
        meta::write_result(path_, s);
        EXPECT_EQ(read_back(),
                  "exitcode:0\n"
                  "time:1.234\n"
                  "time-wall:2.500\n"
                  "max-rss:4096\n"
                  "csw-voluntary:10\n"
                  "csw-forced:3\n");
    }

}  // namespace
