#pragma once
#include <string>

#include "config.h"

namespace rotate {

enum class Profile { Save, Balanced, Max };

const char* profile_id(Profile p);          // "save" | "balanced" | "max"
const char* profile_label(Profile p);       // "Power Save" ...
bool parse_profile(const std::string& s, Profile& out);

struct PowerResult {
  bool ok = false;        // helper ran and exited 0 (individual sysfs writes may still be skipped)
  std::string report;     // helper stdout, one line per applied/skipped write
};

// Runs the privileged helper: `<helper> set <profile>` with ROTATE_SYSFS_ROOT / ROTATE_STATE_DIR set.
PowerResult apply_profile(const Config& cfg, Profile p);
// Reads the profile last recorded by the helper, falling back to cfg.default_profile.
Profile current_profile(const Config& cfg);

}  // namespace rotate
