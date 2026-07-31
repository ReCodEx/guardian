// Unit tests for lock::box_lock — the per-box flock + on-disk init record.
// State (is_initialized / mark_initialized / clear, persistence across reopen,
// truncate-not-unlink) is checked in-process against an injected temp locks
// dir; contention is a death test (the loser exits(2) from the constructor).

#include "lock.hpp"

#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <system_error>

namespace {

    namespace fs = std::filesystem;

    class BoxLockTest : public ::testing::Test {
       protected:
        fs::path dir_;

        void SetUp() override {
            char tmpl[] = "/tmp/box_lock_test_XXXXXX";
            char* made = ::mkdtemp(tmpl);
            ASSERT_NE(made, nullptr);
            dir_ = made;
        }

        void TearDown() override {
            std::error_code ec;
            fs::remove_all(dir_, ec);
        }
    };

    TEST_F(BoxLockTest, fresh_box_is_uninitialized) {
        lock::box_lock l(1, dir_);
        EXPECT_FALSE(l.is_initialized());
    }

    TEST_F(BoxLockTest, mark_then_clear) {
        lock::box_lock l(1, dir_);
        l.mark_initialized();
        EXPECT_TRUE(l.is_initialized());
        l.clear();
        EXPECT_FALSE(l.is_initialized());
    }

    TEST_F(BoxLockTest, record_survives_close_and_reopen) {
        {
            lock::box_lock l(7, dir_);
            l.mark_initialized();
        }  // lock released, fd closed, file persists on tmpfs/disk.
        {
            lock::box_lock l(7, dir_);
            EXPECT_TRUE(l.is_initialized());
        }
    }

    TEST_F(BoxLockTest, clear_truncates_but_does_not_unlink) {
        lock::box_lock l(3, dir_);
        l.mark_initialized();
        l.clear();
        // The record file must still exist after clear() (ftruncate, not
        // unlink).
        EXPECT_TRUE(fs::exists(dir_ / "3"));
    }

    TEST_F(BoxLockTest, second_lock_on_held_box_is_rejected) {
        lock::box_lock held(5, dir_);
        // The forked child opens a second fd on the same path; its new
        // open-file description conflicts with the held flock -> EWOULDBLOCK ->
        // exit(2).
        EXPECT_EXIT(
            { lock::box_lock second(5, dir_); }, ::testing::ExitedWithCode(2),
            "already locked");
    }

    TEST_F(BoxLockTest, distinct_box_ids_do_not_conflict) {
        lock::box_lock a(10, dir_);
        lock::box_lock b(11,
                         dir_);  // different id, different file: both succeed.
        EXPECT_FALSE(a.is_initialized());
        EXPECT_FALSE(b.is_initialized());
    }

}  // namespace
