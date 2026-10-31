// Swivel FSM, evdev decoding (injected event stream), daemon socket protocol, RetroArch pause control,
// and the real SystemActions end to end against fakes (UDP endpoint, fake sysfs, hook scripts).
#include <fcntl.h>
#include <linux/input.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>

#include "client.h"
#include "config.h"
#include "helpers.h"
#include "retroarch.h"
#include "swivel.h"
#include "swiveld.h"
#include "system_actions.h"

using namespace rotate;

using A = Action;
static std::string names(const std::vector<Action>& v) {
  std::string s;
  for (auto a : v) s += std::string(s.empty() ? "" : ",") + action_name(a);
  return s;
}

static void test_fsm() {
  SwivelFsm f;
  CHECK_EQ(names(f.on_switch(false)), std::string(""));  // already open: no-op
  // menu context
  CHECK_EQ(names(f.on_switch(true)), std::string("enter-touch"));
  CHECK_EQ(names(f.on_switch(true)), std::string(""));  // duplicate
  CHECK(f.touch_mode());
  CHECK_EQ(names(f.on_switch(false)), std::string("exit-touch"));
  CHECK(!f.touch_mode());

  // game running, swivel down then up
  CHECK_EQ(names(f.on_context(Context::Game)), std::string(""));
  CHECK_EQ(names(f.on_switch(true)), std::string("save-state,pause-game,panel-off,power-save"));
  CHECK(f.paused());
  CHECK_EQ(names(f.on_switch(true)), std::string(""));
  CHECK_EQ(names(f.on_switch(false)), std::string("power-restore,panel-on,resume-game"));
  CHECK(!f.paused());

  // game exits while the swivel is down (e.g. killed): back to touch mode, panel on
  f.on_switch(true);
  CHECK_EQ(names(f.on_context(Context::Menu)), std::string("power-restore,panel-on,enter-touch"));
  CHECK(f.touch_mode() && !f.paused());
  CHECK_EQ(names(f.on_switch(false)), std::string("exit-touch"));

  // game starts while the swivel is down: leave touch mode, pause at once
  f.on_switch(true);
  CHECK_EQ(names(f.on_context(Context::Game)), std::string("exit-touch,pause-game,panel-off,power-save"));
  CHECK_EQ(names(f.on_switch(false)), std::string("power-restore,panel-on,resume-game"));
  // game ends with swivel up: nothing to do
  CHECK_EQ(names(f.on_context(Context::Menu)), std::string(""));
  CHECK_EQ(names(f.on_context(Context::Menu)), std::string(""));
}

static void put_ev(int fd, int type, int code, int value) {
  struct input_event ev{};
  ev.type = static_cast<__u16>(type);
  ev.code = static_cast<__u16>(code);
  ev.value = value;
  CHECK(write(fd, &ev, sizeof ev) == static_cast<ssize_t>(sizeof ev));
}

static void test_evdev_stream() {
  int p[2];
  CHECK(pipe(p) == 0);
  EvdevSwitch sw(p[0], SW_LID, 1, false);
  CHECK(!sw.initial_state().has_value());  // a pipe is not an evdev node
  put_ev(p[1], EV_SYN, SYN_REPORT, 0);
  put_ev(p[1], EV_KEY, KEY_POWER, 1);              // unrelated key
  put_ev(p[1], EV_SW, SW_TABLET_MODE, 1);          // other switch code
  put_ev(p[1], EV_SW, SW_LID, 1);                  // closed
  put_ev(p[1], EV_SYN, SYN_REPORT, 0);
  put_ev(p[1], EV_SW, SW_LID, 0);                  // open
  bool eof = true;
  auto ev = sw.read_events(&eof);
  CHECK(!eof);
  CHECK_EQ(ev.size(), size_t(2));
  CHECK(ev.size() == 2 && ev[0] == true && ev[1] == false);
  CHECK(sw.read_events(&eof).empty() && !eof);  // drained, non-blocking

  // split frame across two reads
  struct input_event e{};
  e.type = EV_SW; e.code = SW_LID; e.value = 1;
  CHECK(write(p[1], &e, 5) == 5);
  CHECK(sw.read_events(&eof).empty());
  CHECK(write(p[1], reinterpret_cast<char*>(&e) + 5, sizeof e - 5) > 0);
  ev = sw.read_events(&eof);
  CHECK(ev.size() == 1 && ev[0]);
  close(p[1]);
  sw.read_events(&eof);
  CHECK(eof);
  close(p[0]);

  // polarity flipped by config (closed_value=0) and SW_TABLET_MODE selected
  int q[2];
  CHECK(pipe(q) == 0);
  EvdevSwitch tab(q[0], SW_TABLET_MODE, 0, false);
  put_ev(q[1], EV_SW, SW_TABLET_MODE, 0);
  put_ev(q[1], EV_SW, SW_TABLET_MODE, 1);
  auto t = tab.read_events(&eof);
  CHECK(t.size() == 2 && t[0] == true && t[1] == false);
  close(q[0]); close(q[1]);
  CHECK_EQ(switch_code_from_name("LID"), int(SW_LID));
  CHECK_EQ(switch_code_from_name("tablet"), int(SW_TABLET_MODE));
  CHECK_EQ(switch_code_from_name("x"), -1);
}

struct Recorder : ActionSink {
  mutable std::mutex mu;
  std::vector<std::pair<std::string, pid_t>> log;
  void run(Action a, pid_t pid) override {
    std::lock_guard<std::mutex> g(mu);
    log.push_back({action_name(a), pid});
  }
  std::string joined() const {
    std::lock_guard<std::mutex> g(mu);
    std::string s;
    for (auto& e : log) s += (s.empty() ? "" : ",") + e.first;
    return s;
  }
};

static int connect_unix(const std::string& path) {
  int fd = socket(AF_UNIX, SOCK_STREAM, 0);
  sockaddr_un a{};
  a.sun_family = AF_UNIX;
  std::strncpy(a.sun_path, path.c_str(), sizeof a.sun_path - 1);
  CHECK(connect(fd, reinterpret_cast<sockaddr*>(&a), sizeof a) == 0);
  return fd;
}
static std::string drain(int fd) {
  std::string out;
  char buf[256];
  for (;;) {
    pollfd p = {fd, POLLIN, 0};
    if (poll(&p, 1, 50) <= 0) break;
    ssize_t n = recv(fd, buf, sizeof buf, 0);
    if (n <= 0) break;
    out.append(buf, static_cast<size_t>(n));
  }
  return out;
}

static void test_daemon_protocol() {
  TempDir t;
  Recorder rec;
  Swiveld d(rec);
  std::string err;
  CHECK(d.listen_on(t / "run/sw.sock", &err));
  int c = connect_unix(t / "run/sw.sock");
  d.poll_once(200);
  CHECK_EQ(drain(c), std::string("SWIVEL unknown\nMODE menu\nTOUCH off\n"));

  d.handle_switch(false);  // initial EVIOCGSW state
  CHECK_EQ(drain(c), std::string("SWIVEL open\n"));
  d.handle_switch(true);   // menu: touch mode, no process actions
  CHECK_EQ(drain(c), std::string("SWIVEL closed\nTOUCH on\n"));
  CHECK(rec.log.empty());
  d.handle_switch(false);
  CHECK_EQ(drain(c), std::string("SWIVEL open\nTOUCH off\n"));

  send(c, "GAME 4242\n", 10, 0);
  d.poll_once(200);
  CHECK_EQ(d.game_pid(), pid_t(4242));
  CHECK(d.fsm().context() == Context::Game);
  d.handle_switch(true);
  CHECK_EQ(rec.joined(), std::string("save-state,pause-game,panel-off,power-save"));
  CHECK_EQ(rec.log[1].second, pid_t(4242));  // pid reaches the SIGSTOP fallback
  CHECK_EQ(drain(c), std::string("SWIVEL closed\n"));  // no TOUCH on while a game owns the panel
  rec.log.clear();
  d.handle_switch(false);
  CHECK_EQ(rec.joined(), std::string("power-restore,panel-on,resume-game"));
  rec.log.clear();

  // game dies while down: frontend says MENU -> touch mode appears
  d.handle_switch(true);
  rec.log.clear();
  drain(c);
  send(c, "MENU\n", 5, 0);
  d.poll_once(200);
  CHECK_EQ(rec.joined(), std::string("power-restore,panel-on"));
  CHECK_EQ(drain(c), std::string("TOUCH on\n"));
  CHECK_EQ(d.game_pid(), pid_t(0));
  d.handle_switch(false);

  // GET re-sends state; partial lines are buffered
  drain(c);
  send(c, "GE", 2, 0);
  d.poll_once(100);
  send(c, "T\n", 2, 0);
  d.poll_once(100);
  CHECK_EQ(drain(c), std::string("SWIVEL open\nMODE menu\nTOUCH off\n"));

  // frontend crashes mid-game with the swivel down: daemon must not leave the panel dark
  send(c, "GAME 77\n", 8, 0);
  d.poll_once(100);
  d.handle_switch(true);
  rec.log.clear();
  close(c);
  d.poll_once(200);
  CHECK_EQ(rec.joined(), std::string("power-restore,panel-on"));
  CHECK(d.fsm().context() == Context::Menu);
}

static int state_of(pid_t pid) {
  auto s = rotate::read_file("/proc/" + std::to_string(pid) + "/stat");
  if (!s) return '?';
  return (*s)[s->rfind(')') + 2];
}

static void test_retroarch_ctl() {
  {
    FakeRetroArch ra("GET_STATUS PLAYING snes,Super Metroid,crc32=1234");
    RetroArchCtl ctl(ra.port(), 200);
    CHECK(ctl.pause(0) == RetroArchCtl::Method::Net);
    CHECK(wait_until([&] { return ra.got().size() >= 2; }));
    CHECK(ctl.resume(0));
    CHECK(wait_until([&] { return ra.got().size() >= 3; }));
    auto g = ra.got();
    CHECK(g == (std::vector<std::string>{"GET_STATUS", "PAUSE_TOGGLE", "PAUSE_TOGGLE"}));
  }
  {  // the user already paused: do not toggle it back on; resume must not toggle either
    FakeRetroArch ra("GET_STATUS PAUSED snes,Super Metroid,crc32=1234");
    RetroArchCtl ctl(ra.port(), 200);
    CHECK(ctl.pause(0) == RetroArchCtl::Method::AlreadyPaused);
    ctl.resume(0);
    usleep(80 * 1000);
    CHECK(ra.got() == (std::vector<std::string>{"GET_STATUS"}));
  }
  {  // no reply -> SIGSTOP/SIGCONT
    FakeRetroArch ra("");
    pid_t child = fork();
    if (child == 0) { execlp("sleep", "sleep", "30", nullptr); _exit(127); }
    RetroArchCtl ctl(ra.port(), 100);
    CHECK(ctl.pause(child) == RetroArchCtl::Method::Signal);
    CHECK(wait_until([&] { return state_of(child) == 'T'; }));
    CHECK(ctl.resume(child));
    CHECK(wait_until([&] { return state_of(child) != 'T'; }));
    kill(child, SIGKILL);
    waitpid(child, nullptr, 0);
    // no reply and no pid: blind PAUSE_TOGGLE
    CHECK(ctl.pause(0) == RetroArchCtl::Method::Net);
    CHECK(wait_until([&] { return ra.got().back() == "PAUSE_TOGGLE"; }));
  }
}

static void test_system_actions_e2e() {
  TempDir t;
  t.put("sys/class/backlight/panel0/brightness", "150\n");
  t.put("hook.sh", "#!/bin/sh\necho \"$1\" >> " + t / "hook.log" + "\n");
  chmod((t / "hook.sh").c_str(), 0755);
  FakeRetroArch ra("GET_STATUS PLAYING gba,Test,crc32=1");
  Config c;
  c.sysfs_root = t / "sys";
  c.cmd_port = ra.port();
  c.hook_down = c.hook_up = t / "hook.sh";
  c.state_dir = t / "state";
  c.power_helper = "/bin/true";
  SystemActions act(c);
  Swiveld d(act);
  std::string err;
  CHECK(d.listen_on(t / "sw.sock", &err));
  int fe = connect_unix(t / "sw.sock");
  d.poll_once(200);
  d.handle_switch(false);
  send(fe, "GAME 1\n", 7, 0);  // pid 1 is never signalled: the UDP path answers
  d.poll_once(200);

  d.handle_switch(true);
  CHECK_EQ(trim(t.get("sys/class/backlight/panel0/brightness")), std::string("0"));
  CHECK_EQ(t.get("hook.log"), std::string("down\n"));
  CHECK(wait_until([&] { return ra.got().size() >= 3; }));
  CHECK(ra.got() == (std::vector<std::string>{"SAVE_STATE", "GET_STATUS", "PAUSE_TOGGLE"}));

  d.handle_switch(false);
  CHECK_EQ(trim(t.get("sys/class/backlight/panel0/brightness")), std::string("150"));
  CHECK_EQ(t.get("hook.log"), std::string("down\nup\n"));
  CHECK(wait_until([&] { return ra.got().size() >= 4; }));
  CHECK_EQ(ra.got().back(), std::string("PAUSE_TOGGLE"));
  close(fe);
}

static void test_client_thread() {
  TempDir t;
  Recorder rec;
  Swiveld d(rec);
  std::string err;
  CHECK(d.listen_on(t / "sw.sock", &err));
  int ctl[2];
  CHECK(pipe2(ctl, O_NONBLOCK) == 0);
  std::atomic<bool> stop{false};
  std::thread daemon([&] {
    while (!stop) {
      bool ready = false;
      d.poll_once(20, ctl[0], &ready);
      if (ready) {
        char ch;
        while (read(ctl[0], &ch, 1) == 1) d.handle_switch(ch == 'c');
      }
    }
  });
  Lines lines;
  SwivelClient client(t / "sw.sock", [&](const std::string& l) { lines.add(l); }, 50);
  client.start();
  CHECK(wait_until([&] { return lines.has("MODE menu"); }));
  (void)!write(ctl[1], "c", 1);
  CHECK(wait_until([&] { return lines.has("TOUCH on"); }));
  (void)!write(ctl[1], "o", 1);
  CHECK(wait_until([&] { return lines.has("TOUCH off") && lines.find("SWIVEL open") >= 0; }));
  CHECK(client.send("GAME 5"));
  usleep(150 * 1000);  // let the daemon read the line before the switch edge
  (void)!write(ctl[1], "c", 1);
  CHECK(wait_until([&] { return rec.joined().find("pause-game") != std::string::npos; }));
  stop = true;
  daemon.join();
  client.stop();
  close(ctl[0]); close(ctl[1]);
  // daemon absent: client must not spin or crash, send() reports false
  SwivelClient lone(t / "missing.sock", [](const std::string&) {}, 30);
  lone.start();
  usleep(100 * 1000);
  CHECK(!lone.send("GET"));
  lone.stop();
}

int main() {
  test_fsm();
  test_evdev_stream();
  test_daemon_protocol();
  test_retroarch_ctl();
  test_system_actions_e2e();
  test_client_thread();
  TEST_MAIN_END();
}
