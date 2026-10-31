#pragma once
#include "config.h"
#include "retroarch.h"
#include "swiveld.h"
#include "sysinfo.h"

namespace rotate {

// Real side effects: RetroArch UDP/signals, hook scripts, backlight sysfs.
class SystemActions : public ActionSink {
 public:
  explicit SystemActions(const Config& cfg) : cfg_(cfg), ctl_(cfg.cmd_port), backlight_(cfg.sysfs_root) {}
  void run(Action a, pid_t game_pid) override;

 private:
  void hook(const std::string& script, const char* arg);
  const Config& cfg_;
  RetroArchCtl ctl_;
  Backlight backlight_;
};

}  // namespace rotate
