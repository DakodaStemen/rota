#pragma once
#include <sys/types.h>

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace rotate {

// ---- pure state machine ------------------------------------------------------
enum class Context { Menu, Game };

enum class Action {
  SaveState,         // ask RetroArch to write a savestate (battery-death safety)
  PauseGame,
  ResumeGame,
  PowerSave,         // run the swivel-down hook (drop to powersave)
  PowerRestore,      // run the swivel-up hook (restore previous profile)
  PanelOff,
  PanelOn,
  EnterTouchMode,    // tell the frontend to show the touch screen
  ExitTouchMode,
};
const char* action_name(Action a);

// Swivel down == closed (SW_LID value 1, thwonp DT). The FSM only sees the logical bool.
class SwivelFsm {
 public:
  std::vector<Action> on_switch(bool closed);
  std::vector<Action> on_context(Context c);
  bool closed() const { return closed_; }
  bool paused() const { return paused_; }
  bool touch_mode() const { return touch_; }
  Context context() const { return ctx_; }

 private:
  bool closed_ = false;
  bool paused_ = false;   // game paused + panel off + powersave applied
  bool touch_ = false;
  Context ctx_ = Context::Menu;
};

// ---- evdev switch source -------------------------------------------------------
// Reads struct input_event frames from an fd (a real /dev/input/eventN, a uinput device, or a pipe
// carrying injected events) and reports logical "closed" transitions of one EV_SW code.
class EvdevSwitch {
 public:
  // code: SW_LID (0) or SW_TABLET_MODE (1); value == closed_value means closed.
  EvdevSwitch(int fd, int code, int closed_value, bool owns_fd);
  ~EvdevSwitch();
  EvdevSwitch(const EvdevSwitch&) = delete;
  EvdevSwitch& operator=(const EvdevSwitch&) = delete;
  static std::unique_ptr<EvdevSwitch> open_path(const std::string& path, int code, int closed_value, std::string* err);
  // Scans <dev_dir>/event* for a device advertising EV_SW with the given code.
  static std::string find_device(const std::string& dev_dir, int code);

  int fd() const { return fd_; }
  // EVIOCGSW. nullopt if the fd is not an evdev device.
  std::optional<bool> initial_state() const;
  // Drains readable events; returns the closed-state of each matching EV_SW event, in order.
  // Sets *eof when the fd hit EOF/error.
  std::vector<bool> read_events(bool* eof);

 private:
  int fd_, code_, closed_value_;
  bool owns_;
  std::string partial_;
};

int switch_code_from_name(const std::string& name);  // "lid" -> SW_LID, "tablet" -> SW_TABLET_MODE, else -1

}  // namespace rotate
