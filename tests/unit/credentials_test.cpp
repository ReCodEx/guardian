// Unit tests for the credential layer: the startup privilege gate and the
// box-id / box-uid / box-gid range validation in root_credentials_manager.
// Construction + id assignment touch no root-only syscalls (getuid/getgid and
// path concatenation only), so they run in the host (non-root) unit tier.

#include "credentials.hpp"

#include <gtest/gtest.h>

#include "yaml-cpp/yaml.h"

namespace {

    namespace cc = config::config_options;

    // --- startup privilege gate ---------------------------------------------

    TEST(require_root_gate, non_root_invocation_rejected) {
        // The gate must reject a non-root effective uid with a clean message.
        // If the suite happens to run as root the gate would pass and not
        // exit, so skip there — the death path is what we assert.
        if (geteuid() == 0) {
            GTEST_SKIP() << "gate passes as root; death path not exercised";
        }
        EXPECT_EXIT({ credentials::require_root(); },
                    ::testing::ExitedWithCode(2), "Must be started as root");
    }

    // --- box-id range: compat / standalone id -------------------------------

    // Build a manager from a bare cli box-id and run the (root-syscall-free)
    // id assignment.
    config::credentials_config creds_from_box_id(std::size_t id) {
        cli::cli_options o;
        o.box_id = id;
        return config::credentials_config(o);
    }

    TEST(box_id_range, in_range_id_derives_box_uid) {
        auto cfg = creds_from_box_id(4000);
        credentials::root_credentials_manager mgr(cfg);
        mgr.run();
        EXPECT_EQ(mgr.box_id(), 4000u);
        EXPECT_EQ(mgr.box_uid(), 64000u);  // 4000 + 60000
        EXPECT_EQ(mgr.box_gid(), 64000u);
    }

    TEST(box_id_range, max_id_accepted) {
        auto cfg = creds_from_box_id(5000);
        credentials::root_credentials_manager mgr(cfg);
        mgr.run();
        EXPECT_EQ(mgr.box_uid(), 65000u);  // top of the band
    }

    TEST(box_id_range, over_max_id_rejected) {
        auto cfg = creds_from_box_id(5001);
        credentials::root_credentials_manager mgr(cfg);
        EXPECT_EXIT({ mgr.run(); }, ::testing::ExitedWithCode(2),
                    "Sandbox ID 5001 out of range");
    }

    TEST(box_id_range, huge_id_rejected_before_narrowing) {
        // A size_t id above uid_t range must be caught on the raw value, not
        // silently truncated into a valid-looking box_id.
        auto cfg = creds_from_box_id(std::size_t{1} << 33);
        credentials::root_credentials_manager mgr(cfg);
        EXPECT_EXIT({ mgr.run(); }, ::testing::ExitedWithCode(2),
                    "out of range");
    }

    // --- box-uid / box-gid explicit overrides (standalone YAML) -------------

    // Standalone config can pin box uid/gid via as-uid / as-gid; those are
    // validated too — no admin escape hatch to a privileged id.
    config::credentials_config creds_from_yaml(int id, int as_uid, int as_gid) {
        YAML::Node n;
        n[cc::credentials::INSTANCE_ID] = id;
        if (as_uid >= 0) { n[cc::credentials::BOX_UID] = as_uid; }
        if (as_gid >= 0) { n[cc::credentials::BOX_GID] = as_gid; }
        return config::credentials_config(n);
    }

    TEST(box_uid_override, in_range_override_accepted) {
        auto cfg = creds_from_yaml(5, 61234, 61234);
        credentials::root_credentials_manager mgr(cfg);
        mgr.run();
        EXPECT_EQ(mgr.box_uid(), 61234u);
        EXPECT_EQ(mgr.box_gid(), 61234u);
    }

    TEST(box_uid_override, privileged_uid_rejected) {
        auto cfg = creds_from_yaml(5, 0, -1);  // as-uid: 0
        credentials::root_credentials_manager mgr(cfg);
        EXPECT_EXIT({ mgr.run(); }, ::testing::ExitedWithCode(2),
                    "Configured box_uid 0 out of range");
    }

    TEST(box_uid_override, out_of_band_gid_rejected) {
        auto cfg = creds_from_yaml(5, -1, 1000);  // as-gid in the user range
        credentials::root_credentials_manager mgr(cfg);
        EXPECT_EXIT({ mgr.run(); }, ::testing::ExitedWithCode(2),
                    "Configured box_gid 1000 out of range");
    }

}  // namespace
