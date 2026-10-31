// End to end with a REAL evdev device: creates a uinput switch (EV_SW/SW_LID), then
//  1. reads it through EvdevSwitch (EVIOCGSW + events), and
//  2. runs the real rotate-swiveld binary on it and drives the whole swivel flow through the socket.
// Needs root (/dev/uinput, /dev/input/event*); tests/run_uinput.sh handles sudo and skips (77) if unavailable.
#include <dirent.h>
#include <fcntl.h>
#include <linux/input.h>
#include <linux/uinput.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/wait.h>

#include "client.h"
#include "helpers.h"
#include "swivel.h"

using namespace rotate;

static const char* kName = "rotate-test-gpio-keys";

static void emit(int fd, int type, int code, int value) {
  struct input_event ev{};
  ev.type = static_cast<__u16>(type);
  ev.code = static_cast<__u16>(code);
  ev.value = value;
  (void)!write(fd, &ev, sizeof ev);
}
static void set_lid(int ufd, int v) {
  emit(ufd, EV_SW, SW_LID, v);
  emit(ufd, EV_SYN, SYN_REPORT, 0);
}

static std::string find_by_name(const char* name) {
  DIR* d = opendir("/dev/input");
  std::string found;
  while (d && found.empty()) {
    auto* e = readdir(d);
    if (!e) break;
    if (strncmp(e->d_name, "event", 5)) continue;
    std::string p = std::string("/dev/input/") + e->d_name;
    int fd = open(p.c_str(), O_RDONLY | O_CLOEXEC);
    if (fd < 0) continue;
    char n[128] = {0};
    if (ioctl(fd, EVIOCGNAME(sizeof n), n) >= 0 && !strcmp(n, name)) found = p;
    close(fd);
  }
  if (d) closedir(d);
  return found;
}

int main() {
  int ufd = open("/dev/uinput", O_WRONLY | O_NONBLOCK);
  if (ufd < 0) {
    std::fprintf(stderr, "SKIP: cannot open /dev/uinput (%s)\n", std::strerror(errno));
    return 77;
  }
  ioctl(ufd, UI_SET_EVBIT, EV_SW);
  ioctl(ufd, UI_SET_SWBIT, SW_LID);
  ioctl(ufd, UI_SET_EVBIT, EV_SYN);
  struct uinput_setup us{};
  us.id.bustype = BUS_VIRTUAL;
  us.id.vendor = 0x1d6b;
  us.id.product = 0x0001;
  std::strncpy(us.name, kName, UINPUT_MAX_NAME_SIZE - 1);
  CHECK(ioctl(ufd, UI_DEV_SETUP, &us) == 0);
  CHECK(ioctl(ufd, UI_DEV_CREATE) == 0);
  std::string node;
  CHECK(wait_until([&] { return !(node = find_by_name(kName)).empty(); }));
  std::printf("uinput device node: %s\n", node.c_str());
  CHECK(!node.empty());
  if (node.empty()) return 1;

  // ---- part 1: EvdevSwitch on a real evdev node
  {
    std::string err;
    auto sw = EvdevSwitch::open_path(node, SW_LID, 1, &err);
    CHECK(sw != nullptr);
    CHECK(sw->initial_state().has_value() && *sw->initial_state() == false);  // EVIOCGSW: open
    set_lid(ufd, 1);
    CHECK(wait_until([&] { pollfd p = {sw->fd(), POLLIN, 0}; return poll(&p, 1, 0) > 0; }));
    bool eof;
    auto ev = sw->read_events(&eof);
    CHECK(ev.size() == 1 && ev[0] == true);
    CHECK(*sw->initial_state() == true);  // EVIOCGSW now reports closed
    EvdevSwitch inverted(dup(sw->fd()), SW_LID, 0, true);
    CHECK(*inverted.initial_state() == false);  // same hardware state, flipped polarity
    set_lid(ufd, 0);
    CHECK(wait_until([&] { pollfd p = {sw->fd(), POLLIN, 0}; return poll(&p, 1, 0) > 0; }));
    ev = sw->read_events(&eof);
    CHECK(ev.size() == 1 && ev[0] == false);
    CHECK(!EvdevSwitch::find_device("/dev/input", SW_LID).empty());
  }

  // ---- part 2: the real daemon binary
  TempDir t;
  t.put("sys/class/backlight/panel0/brightness", "200\n");
  t.put("hook.sh", "#!/bin/sh\necho \"$1\" >> " + t / "hook.log" + "\n");
  chmod((t / "hook.sh").c_str(), 0755);
  FakeRetroArch ra("GET_STATUS PLAYING test,Game,crc32=0");
  t.put("frontend.ini", "[paths]\nsysfs_root = " + t / "sys" + "\nstate_dir = " + t / "state" + "\nsocket = " + t / "sw.sock" +
                          "\n[retroarch]\ncmd_port = " + std::to_string(ra.port()) + "\n[swivel]\nhook_down = " + t / "hook.sh" +
                          "\nhook_up = " + t / "hook.sh" + "\n");
  pid_t dpid = fork();
  if (dpid == 0) {
    int null = open("/dev/null", O_WRONLY);
    dup2(null, 2);
    execl(ROTATE_SWIVELD_BIN, "rotate-swiveld", "--config", (t / "frontend.ini").c_str(), "--device", node.c_str(), (char*)nullptr);
    _exit(127);
  }
  CHECK(wait_until([&] { struct stat st; return stat((t / "sw.sock").c_str(), &st) == 0; }));

  Lines lines;
  SwivelClient client(t / "sw.sock", [&](const std::string& l) { lines.add(l); }, 50);
  client.start();
  CHECK(wait_until([&] { return lines.has("SWIVEL open"); }));  // initial EVIOCGSW state

  set_lid(ufd, 1);
  CHECK(wait_until([&] { return lines.has("TOUCH on"); }));    // menu + swivel down -> touch mode
  set_lid(ufd, 0);
  CHECK(wait_until([&] { return lines.find("TOUCH off", lines.find("TOUCH on")) >= 0; }));

  CHECK(client.send("GAME " + std::to_string(getpid())));      // our own pid; UDP path must be used, never SIGSTOP
  usleep(100 * 1000);
  set_lid(ufd, 1);                                              // swivel down with a game running
  CHECK(wait_until([&] { return trim(t.get("sys/class/backlight/panel0/brightness")) == "0"; }));
  CHECK(wait_until([&] { return ra.got().size() >= 3; }));
  CHECK(ra.got() == (std::vector<std::string>{"SAVE_STATE", "GET_STATUS", "PAUSE_TOGGLE"}));
  CHECK(wait_until([&] { return t.get("hook.log") == "down\n"; }));
  set_lid(ufd, 0);                                              // swivel up
  CHECK(wait_until([&] { return trim(t.get("sys/class/backlight/panel0/brightness")) == "200"; }));
  CHECK(wait_until([&] { return ra.got().size() >= 4; }));
  CHECK_EQ(ra.got().back(), std::string("PAUSE_TOGGLE"));
  CHECK(wait_until([&] { return t.get("hook.log") == "down\nup\n"; }));

  client.stop();
  kill(dpid, SIGTERM);
  int st = 0;
  waitpid(dpid, &st, 0);
  CHECK(WIFEXITED(st) && WEXITSTATUS(st) == 0);
  struct stat sb;
  CHECK(stat((t / "sw.sock").c_str(), &sb) != 0);  // socket cleaned up

  ioctl(ufd, UI_DEV_DESTROY);
  close(ufd);
  TEST_MAIN_END();
}
