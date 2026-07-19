// Unit tests for file_utils helpers. Focus: lchown_tree, the box ownership-dance
// walker, whose load-bearing security property is that it never follows a
// symlink (the untrusted task may plant one in box/). Runs in the host tier:
// chowning a tree to *self* needs no privilege, and the no-follow property is
// provable without root by pointing a symlink at a file we do not own.

#include "utils.hpp"

#include <gtest/gtest.h>

#include <sys/stat.h>
#include <unistd.h>

#include <filesystem>
#include <fstream>

namespace {

    namespace fs = std::filesystem;

    // A unique scratch dir under the system temp dir, removed on destruction.
    struct scratch {
        fs::path root;
        scratch() {
            root = fs::temp_directory_path() /
                   ("lchown_tree_test_" + std::to_string(::getpid()) + "_" +
                    std::to_string(reinterpret_cast<uintptr_t>(this)));
            fs::create_directories(root);
        }
        ~scratch() {
            std::error_code ec;
            fs::remove_all(root, ec);
        }
    };

    uid_t owner_of(const fs::path& p) {
        struct stat st{};
        EXPECT_EQ(::lstat(p.c_str(), &st), 0) << p;
        return st.st_uid;
    }

    TEST(lchown_tree, recurses_over_nested_tree) {
        scratch s;
        std::ofstream(s.root / "top.txt") << "a";
        fs::create_directories(s.root / "sub" / "deep");
        std::ofstream(s.root / "sub" / "deep" / "leaf.txt") << "b";

        // Chown to self: a no-op ownership-wise, but exercises the full walk
        // and must not terminate. Reaching the asserts proves it returned.
        file_utils::lchown_tree(s.root, ::geteuid(), ::getegid());

        EXPECT_EQ(owner_of(s.root / "top.txt"), ::geteuid());
        EXPECT_EQ(owner_of(s.root / "sub" / "deep" / "leaf.txt"), ::geteuid());
    }

    TEST(lchown_tree, does_not_follow_symlink) {
        // A symlink to a file we do NOT own (root-owned /etc/passwd). lchown
        // retargets the LINK (which we own) and succeeds; a following chown
        // would hit the root-owned target with EPERM and terminate(). So a
        // clean return is the proof of no-follow. As root the distinction is
        // moot (both succeed) — the assertion still holds.
        const fs::path target = "/etc/passwd";
        if (!fs::exists(target) || owner_of(target) == ::geteuid()) {
            GTEST_SKIP() << "need a stable not-owned file to prove no-follow";
        }
        scratch s;
        fs::create_symlink(target, s.root / "link");

        file_utils::lchown_tree(s.root, ::geteuid(), ::getegid());

        // The link's own metadata was retargeted; the target is untouched.
        EXPECT_EQ(owner_of(s.root / "link"), ::geteuid());
        EXPECT_NE(owner_of(target), ::geteuid());  // still root-owned
    }

}  // namespace
