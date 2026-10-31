#pragma once
#include <sys/types.h>

#include <string>
#include <vector>

#include "config.h"
#include "library.h"

namespace rotate {

// ---- launching -----------------------------------------------------------
struct LaunchSpec {
  std::vector<std::string> argv;
  std::string append_config_path;
  std::string append_config_text;
};

// Builds `retroarch -L <core> --appendconfig <state>/retroarch-rotate.cfg [--config base] <rom>`.
// The append config enables the network command port and (resume_on_wake) savestate auto save/load.
LaunchSpec build_launch(const Config& cfg, const SystemDef& sys, const Game& game);
// Writes the append config; true on success.
bool prepare_launch(const LaunchSpec& spec);
// fork+exec; returns pid or -1.
pid_t spawn(const LaunchSpec& spec);
// Waits for pid, returns exit code (or 128+signal).
int wait_child(pid_t pid);

// ---- runtime control -------------------------------------------------------
// Pause/resume a running RetroArch. Order of preference:
//   1. UDP 55355 GET_STATUS, then PAUSE_TOGGLE only if it reports PLAYING (idempotent)
//   2. no reply (network_cmd_enable off / old RetroArch): SIGSTOP/SIGCONT on the pid
//   3. no reply and no pid: blind PAUSE_TOGGLE
class RetroArchCtl {
 public:
  enum class Method { None, Net, Signal, AlreadyPaused };
  explicit RetroArchCtl(int port, int reply_timeout_ms = 250, std::string host = "127.0.0.1")
      : port_(port), timeout_ms_(reply_timeout_ms), host_(std::move(host)) {}
  Method pause(pid_t pid);
  bool resume(pid_t pid);
  bool save_state();                        // sends SAVE_STATE (fire and forget)
  Method method() const { return method_; }
  std::string query_status();               // raw GET_STATUS reply, "" on timeout
  bool send(const std::string& cmd);

 private:
  int port_;
  int timeout_ms_;
  std::string host_;
  Method method_ = Method::None;
};

}  // namespace rotate
