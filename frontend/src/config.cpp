#include "config.h"

#include <cstdlib>

#include "util.h"

namespace rotate {

static bool as_bool(const std::string& v, bool def) {
  auto l = to_lower(v);
  if (l == "1" || l == "true" || l == "yes" || l == "on") return true;
  if (l == "0" || l == "false" || l == "no" || l == "off") return false;
  return def;
}
static int as_int(const std::string& v, int def) {
  if (v.empty()) return def;
  char* end = nullptr;
  long n = std::strtol(v.c_str(), &end, 10);
  return (end && *end == 0) ? static_cast<int>(n) : def;
}

Config Config::from_ini(const Ini& i) {
  Config c;
  c.roms_root = i.get("paths", "roms_root", c.roms_root);
  c.state_dir = i.get("paths", "state_dir", c.state_dir);
  c.sysfs_root = i.get("paths", "sysfs_root", c.sysfs_root);
  c.socket_path = i.get("paths", "socket", c.socket_path);
  c.font_regular = i.get("paths", "font_regular", c.font_regular);
  c.font_bold = i.get("paths", "font_bold", c.font_bold);
  c.retroarch_bin = i.get("retroarch", "bin", c.retroarch_bin);
  c.retroarch_cfg = i.get("retroarch", "config", c.retroarch_cfg);
  c.cores_dir = i.get("retroarch", "cores_dir", c.cores_dir);
  c.cmd_port = as_int(i.get("retroarch", "cmd_port"), c.cmd_port);
  c.resume_on_wake = as_bool(i.get("retroarch", "resume_on_wake"), c.resume_on_wake);
  c.power_helper = i.get("power", "helper", c.power_helper);
  c.default_profile = i.get("power", "default", c.default_profile);
  c.switch_device = i.get("swivel", "device", c.switch_device);
  c.switch_code = to_lower(i.get("swivel", "code", c.switch_code));
  c.closed_value = as_int(i.get("swivel", "closed_value"), c.closed_value);
  c.save_on_close = as_bool(i.get("swivel", "save_on_close"), c.save_on_close);
  c.hook_down = i.get("swivel", "hook_down", c.hook_down);
  c.hook_up = i.get("swivel", "hook_up", c.hook_up);
  int rot = as_int(i.get("swivel", "touch_rotation"), c.touch_rotation);
  c.touch_rotation = ((rot % 360) + 360) % 360;
  if (c.touch_rotation % 90 != 0) c.touch_rotation = 0;
  for (const auto& sec : i.sections()) {
    if (sec.rfind("system.", 0) != 0) continue;
    SystemDef s;
    s.id = sec.substr(7);
    s.name = i.get(sec, "name", s.id);
    s.core = i.get(sec, "core");
    for (auto e : split_ws(i.get(sec, "extensions"))) {
      if (!e.empty() && e[0] == '.') e.erase(0, 1);
      s.exts.push_back(to_lower(e));
    }
    if (!s.id.empty() && !s.core.empty()) c.systems.push_back(s);
  }
  return c;
}

bool Config::load(const std::string& path, Config& out, std::string* err) {
  Ini ini;
  if (!Ini::load(path, ini, err)) return false;
  out = from_ini(ini);
  return true;
}

std::string Config::core_path(const SystemDef& s) const {
  if (s.core.find('/') != std::string::npos) return s.core;
  if (ends_with(s.core, ".so")) return cores_dir + "/" + s.core;
  return cores_dir + "/" + s.core + "_libretro.so";
}

const SystemDef* Config::find_system(const std::string& id) const {
  for (const auto& s : systems)
    if (s.id == id) return &s;
  return nullptr;
}

}  // namespace rotate
