#pragma once
#include <sys/types.h>

#include <map>
#include <string>
#include <vector>

#include "swivel.h"

namespace rotate {

// Side effects of FSM actions. The real implementation is SystemActions (system_actions.h);
// tests inject a recorder.
class ActionSink {
 public:
  virtual ~ActionSink() = default;
  virtual void run(Action a, pid_t game_pid) = 0;
};

// Wire protocol (UTF-8 lines over an AF_UNIX SOCK_STREAM socket):
//   daemon -> client, on connect:  "SWIVEL closed|open|unknown"  "MODE menu|game"  "TOUCH on|off"
//   daemon -> client, on change:   "SWIVEL closed|open"  and  "TOUCH on|off"
//   client -> daemon:  "GAME <pid>"   a game process started (pid used for the SIGSTOP fallback)
//                      "MENU"         the game ended; the frontend owns the screen again
//                      "GET"          re-send the connect-time lines
// A client that disconnects while a game is registered counts as "MENU".
class Swiveld {
 public:
  explicit Swiveld(ActionSink& sink) : sink_(sink) {}
  ~Swiveld();
  bool listen_on(const std::string& path, std::string* err);

  // Feed one logical switch transition (closed == swivel down). Called for the initial EVIOCGSW state too.
  void handle_switch(bool closed);
  // One poll round over the listening socket, clients and (optionally) an extra fd. Returns the number of
  // events handled. extra_fd < 0: ignored. Sets *extra_ready when extra_fd was readable.
  int poll_once(int timeout_ms, int extra_fd = -1, bool* extra_ready = nullptr);

  const SwivelFsm& fsm() const { return fsm_; }
  pid_t game_pid() const { return game_pid_; }

 private:
  struct Client {
    std::string inbuf;
  };
  void apply(const std::vector<Action>& acts);
  void broadcast(const std::string& line);
  void greet(int fd);
  void on_line(int fd, const std::string& line);
  void drop(int fd);

  ActionSink& sink_;
  SwivelFsm fsm_;
  bool state_known_ = false;
  int listen_fd_ = -1;
  std::string sock_path_;
  std::map<int, Client> clients_;
  pid_t game_pid_ = 0;
};

}  // namespace rotate
