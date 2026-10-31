// rotate-power-helper against fake sysfs trees, plus the swivel hooks.
#include "check.h"
#include "config.h"
#include "power.h"

using namespace rotate;

static const std::string kHelper = std::string(ROTATE_SCRIPTS_DIR) + "/rotate-power-helper";

static void cpu(const TempDir& t, int n, const std::string& govs, const std::string& freqs,
                const std::string& hwmin, const std::string& hwmax) {
  std::string d = "devices/system/cpu/cpu" + std::to_string(n) + "/cpufreq/";
  t.put(d + "scaling_governor", "schedutil\n");
  t.put(d + "scaling_available_governors", govs + "\n");
  if (!freqs.empty()) t.put(d + "scaling_available_frequencies", freqs + "\n");
  t.put(d + "cpuinfo_min_freq", hwmin + "\n");
  t.put(d + "cpuinfo_max_freq", hwmax + "\n");
  t.put(d + "scaling_max_freq", hwmax + "\n");
}
static std::string g(const TempDir& t, int n, const char* f) {
  return trim(t.get("devices/system/cpu/cpu" + std::to_string(n) + "/cpufreq/" + f));
}

int main() {
  {  // normal board: two clusters, discrete OPPs
    TempDir t;
    cpu(t, 0, "performance schedutil powersave", "500000 1000000 1500000 2000000", "500000", "2000000");
    cpu(t, 4, "performance schedutil powersave", "500000 1000000 1500000 2000000", "500000", "2000000");
    Config c;
    c.power_helper = kHelper;
    c.sysfs_root = t.path;
    c.state_dir = t / "state";

    auto r = apply_profile(c, Profile::Save);
    CHECK(r.ok);
    CHECK_EQ(g(t, 0, "scaling_governor"), std::string("powersave"));
    CHECK_EQ(g(t, 0, "scaling_max_freq"), std::string("1000000"));  // 50% of 2 GHz
    CHECK_EQ(g(t, 4, "scaling_max_freq"), std::string("1000000"));
    CHECK(current_profile(c) == Profile::Save);

    r = apply_profile(c, Profile::Balanced);
    CHECK_EQ(g(t, 0, "scaling_governor"), std::string("schedutil"));
    CHECK_EQ(g(t, 0, "scaling_max_freq"), std::string("1500000"));  // largest OPP <= 85% (1.7 GHz)
    r = apply_profile(c, Profile::Max);
    CHECK_EQ(g(t, 4, "scaling_governor"), std::string("performance"));
    CHECK_EQ(g(t, 4, "scaling_max_freq"), std::string("2000000"));
    CHECK(current_profile(c) == Profile::Max);
    CHECK(r.report.find("ok   cpu0 governor = performance") != std::string::npos);

    // hooks: down remembers + saves, up restores; a second down must not clobber the remembered profile
    std::vector<std::string> env = {"ROTATE_SYSFS_ROOT=" + t.path, "ROTATE_STATE_DIR=" + t / "state",
                                    "ROTATE_POWER_HELPER=" + kHelper};
    auto h = run_capture({std::string(ROTATE_SCRIPTS_DIR) + "/swivel-down", "down"}, env, 5000);
    CHECK_EQ(h.exit_code, 0);
    CHECK_EQ(g(t, 0, "scaling_governor"), std::string("powersave"));
    h = run_capture({std::string(ROTATE_SCRIPTS_DIR) + "/swivel-down", "down"}, env, 5000);
    CHECK_EQ(trim(t.get("state/power-profile.prev")), std::string("max"));
    h = run_capture({std::string(ROTATE_SCRIPTS_DIR) + "/swivel-up", "up"}, env, 5000);
    CHECK_EQ(h.exit_code, 0);
    CHECK_EQ(g(t, 0, "scaling_governor"), std::string("performance"));
    CHECK_EQ(g(t, 0, "scaling_max_freq"), std::string("2000000"));
    CHECK_EQ(t.get("state/power-profile.prev"), std::string("<missing>"));
  }
  {  // fixed-frequency board (research: T618 may have no DVFS) + governor missing from the list
    TempDir t;
    cpu(t, 0, "performance", "", "1600000", "1600000");
    Config c;
    c.power_helper = kHelper;
    c.sysfs_root = t.path;
    c.state_dir = t / "state";
    auto r = apply_profile(c, Profile::Save);
    CHECK(r.ok);
    CHECK(r.report.find("fixed frequency") != std::string::npos);
    CHECK(r.report.find("none of: powersave available") != std::string::npos);
    CHECK_EQ(g(t, 0, "scaling_governor"), std::string("schedutil"));  // untouched
    CHECK_EQ(g(t, 0, "scaling_max_freq"), std::string("1600000"));
    CHECK(current_profile(c) == Profile::Save);  // still recorded
    r = apply_profile(c, Profile::Balanced);     // schedutil missing, ondemand missing -> skip
    CHECK(r.report.find("none of: schedutil ondemand") != std::string::npos);
  }
  {  // no cpufreq at all, and read-only attribute files: best effort, still exit 0
    TempDir t;
    Config c;
    c.power_helper = kHelper;
    c.sysfs_root = t.path;
    c.state_dir = t / "state";
    auto r = apply_profile(c, Profile::Max);
    CHECK(r.ok);
    CHECK(r.report.find("no cpufreq policy") != std::string::npos);
    cpu(t, 0, "performance powersave", "", "500000", "2000000");
    (void)!std::system(("chmod a-w " + t / "devices/system/cpu/cpu0/cpufreq/scaling_governor").c_str());
    r = apply_profile(c, Profile::Save);
    CHECK(r.ok);
    if (geteuid() != 0) CHECK(r.report.find("skip cpu0 governor") != std::string::npos);
    CHECK_EQ(g(t, 0, "scaling_max_freq"), std::string("1000000"));  // no OPP list: raw 50% target
  }
  {  // bad input
    Config c;
    c.power_helper = kHelper;
    auto r = run_capture({kHelper, "set", "turbo"}, {}, 5000);
    CHECK_EQ(r.exit_code, 2);
    r = run_capture({kHelper}, {}, 5000);
    CHECK_EQ(r.exit_code, 2);
    Profile p;
    CHECK(parse_profile(" Max ", p) && p == Profile::Max);
    CHECK(!parse_profile("x", p));
  }
  TEST_MAIN_END();
}
