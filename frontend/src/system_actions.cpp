#include "system_actions.h"

#include <unistd.h>

#include <cstdio>

#include "util.h"

namespace rotate {

void SystemActions::hook(const std::string& script, const char* arg) {
  if (script.empty()) return;
  auto r = run_capture({script, arg},
                       {"ROTATE_SYSFS_ROOT=" + cfg_.sysfs_root, "ROTATE_STATE_DIR=" + cfg_.state_dir,
                        "ROTATE_POWER_HELPER=" + cfg_.power_helper},
                       5000);
  if (r.exit_code != 0) std::fprintf(stderr, "swiveld: hook %s %s failed (exit %d, timeout %d)\n", script.c_str(), arg, r.exit_code, r.timed_out);
}

void SystemActions::run(Action a, pid_t pid) {
  std::fprintf(stderr, "swiveld: action %s\n", action_name(a));
  switch (a) {
    case Action::SaveState:
      if (cfg_.save_on_close) {
        ctl_.save_state();
        usleep(150 * 1000);  // let RetroArch pick the command up before SIGSTOP/pause can land
      }
      break;
    case Action::PauseGame: ctl_.pause(pid); break;
    case Action::ResumeGame: ctl_.resume(pid); break;
    case Action::PowerSave: hook(cfg_.hook_down, "down"); break;
    case Action::PowerRestore: hook(cfg_.hook_up, "up"); break;
    case Action::PanelOff:
      if (!backlight_.off()) std::fprintf(stderr, "swiveld: no writable backlight under %s\n", cfg_.sysfs_root.c_str());
      break;
    case Action::PanelOn: backlight_.on(); break;
    default: break;  // touch-mode actions are broadcast, not executed
  }
}

}  // namespace rotate
