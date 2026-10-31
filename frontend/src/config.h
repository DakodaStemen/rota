#pragma once
#include <string>
#include <vector>

#include "ini.h"

namespace rotate {

struct SystemDef {
  std::string id;    // directory name under roms_root
  std::string name;  // display name
  std::string core;  // libretro core: file name stem or path
  std::vector<std::string> exts;  // lowercase, no dot
};

struct Config {
  // [paths]
  std::string roms_root = "/roms";
  std::string state_dir = "/var/lib/rotate";   // history, power profile, generated RetroArch cfg
  std::string sysfs_root = "/sys";             // root for cpufreq, backlight, battery, thermal
  std::string socket_path = "/run/rotate-swiveld.sock";
  std::string font_regular = "";               // empty: search known locations
  std::string font_bold = "";
  // [retroarch]
  std::string retroarch_bin = "/usr/bin/retroarch";
  std::string retroarch_cfg = "";              // optional base config (--config)
  std::string cores_dir = "/usr/lib/libretro";
  int cmd_port = 55355;
  bool resume_on_wake = true;                  // savestate_auto_save/load in the generated append config
  // [power]
  std::string power_helper = "/usr/lib/rotate/rotate-power-helper";
  std::string default_profile = "balanced";
  // [swivel]
  std::string switch_device = "";              // empty: auto-detect by capability
  std::string switch_code = "lid";             // lid (SW_LID) | tablet (SW_TABLET_MODE)
  int closed_value = 1;                        // SW_LID: 1 = closed = swivel down (thwonp DT)
  bool save_on_close = true;                   // send SAVE_STATE before pausing
  std::string hook_down = "";
  std::string hook_up = "";
  int touch_rotation = 90;                     // degrees clockwise applied to touch mode: 0/90/180/270
  std::vector<SystemDef> systems;

  static Config from_ini(const Ini& ini);
  static bool load(const std::string& path, Config& out, std::string* err = nullptr);
  std::string core_path(const SystemDef& s) const;
  const SystemDef* find_system(const std::string& id) const;
};

}  // namespace rotate
