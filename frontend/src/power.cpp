#include "power.h"

#include "util.h"

namespace rotate {

const char* profile_id(Profile p) {
  switch (p) {
    case Profile::Save: return "save";
    case Profile::Balanced: return "balanced";
    case Profile::Max: return "max";
  }
  return "balanced";
}

const char* profile_label(Profile p) {
  switch (p) {
    case Profile::Save: return "Power Save";
    case Profile::Balanced: return "Balanced";
    case Profile::Max: return "Max Performance";
  }
  return "Balanced";
}

bool parse_profile(const std::string& s, Profile& out) {
  auto l = to_lower(trim(s));
  if (l == "save") out = Profile::Save;
  else if (l == "balanced") out = Profile::Balanced;
  else if (l == "max") out = Profile::Max;
  else return false;
  return true;
}

PowerResult apply_profile(const Config& cfg, Profile p) {
  auto r = run_capture({cfg.power_helper, "set", profile_id(p)},
                       {"ROTATE_SYSFS_ROOT=" + cfg.sysfs_root, "ROTATE_STATE_DIR=" + cfg.state_dir}, 5000);
  return {r.exit_code == 0, r.output};
}

Profile current_profile(const Config& cfg) {
  Profile p = Profile::Balanced;
  parse_profile(cfg.default_profile, p);
  if (auto s = read_line(cfg.state_dir + "/power-profile")) parse_profile(*s, p);
  return p;
}

}  // namespace rotate
