// rotate-swiveld: reads the hall switch from evdev, drives the SwivelFsm, serves state on a Unix socket.
#include <linux/input.h>
#include <signal.h>

#include <cstdio>
#include <cstring>

#include "config.h"
#include "swivel.h"
#include "swiveld.h"
#include "system_actions.h"

using namespace rotate;

static volatile sig_atomic_t g_stop = 0;
static void on_signal(int) { g_stop = 1; }

int main(int argc, char** argv) {
  std::string cfg_path = "/etc/rotate/frontend.ini", device;
  for (int i = 1; i < argc; ++i) {
    if (!std::strcmp(argv[i], "--config") && i + 1 < argc) cfg_path = argv[++i];
    else if (!std::strcmp(argv[i], "--device") && i + 1 < argc) device = argv[++i];
    else {
      std::fprintf(stderr, "usage: rotate-swiveld [--config FILE] [--device /dev/input/eventN]\n");
      return 2;
    }
  }
  Config cfg;
  std::string err;
  if (!Config::load(cfg_path, cfg, &err)) {
    std::fprintf(stderr, "rotate-swiveld: %s\n", err.c_str());
    return 1;
  }
  int code = switch_code_from_name(cfg.switch_code);
  if (code < 0) {
    std::fprintf(stderr, "rotate-swiveld: bad [swivel] code '%s'\n", cfg.switch_code.c_str());
    return 1;
  }
  if (device.empty()) device = cfg.switch_device;
  if (device.empty()) device = EvdevSwitch::find_device("/dev/input", code);
  if (device.empty()) {
    std::fprintf(stderr, "rotate-swiveld: no input device with the EV_SW code found\n");
    return 1;
  }
  auto sw = EvdevSwitch::open_path(device, code, cfg.closed_value, &err);
  if (!sw) {
    std::fprintf(stderr, "rotate-swiveld: %s\n", err.c_str());
    return 1;
  }
  SystemActions actions(cfg);
  Swiveld d(actions);
  if (!d.listen_on(cfg.socket_path, &err)) {
    std::fprintf(stderr, "rotate-swiveld: %s\n", err.c_str());
    return 1;
  }
  struct sigaction sa;
  std::memset(&sa, 0, sizeof sa);
  sa.sa_handler = on_signal;  // no SA_RESTART: poll() must return EINTR
  sigaction(SIGINT, &sa, nullptr);
  sigaction(SIGTERM, &sa, nullptr);

  if (auto st = sw->initial_state()) d.handle_switch(*st);
  std::fprintf(stderr, "rotate-swiveld: watching %s, socket %s\n", device.c_str(), cfg.socket_path.c_str());
  while (!g_stop) {  // blocks in poll(); zero wakeups while idle
    bool ready = false;
    d.poll_once(-1, sw->fd(), &ready);
    if (!ready) continue;
    bool eof = false;
    for (bool closed : sw->read_events(&eof)) d.handle_switch(closed);
    if (eof) {
      std::fprintf(stderr, "rotate-swiveld: input device went away\n");
      return 1;
    }
  }
  return 0;
}
