#include "swivel.h"

#include <dirent.h>
#include <fcntl.h>
#include <linux/input.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>

#include "util.h"

namespace rotate {

const char* action_name(Action a) {
  switch (a) {
    case Action::SaveState: return "save-state";
    case Action::PauseGame: return "pause-game";
    case Action::ResumeGame: return "resume-game";
    case Action::PowerSave: return "power-save";
    case Action::PowerRestore: return "power-restore";
    case Action::PanelOff: return "panel-off";
    case Action::PanelOn: return "panel-on";
    case Action::EnterTouchMode: return "enter-touch";
    case Action::ExitTouchMode: return "exit-touch";
  }
  return "?";
}

std::vector<Action> SwivelFsm::on_switch(bool closed) {
  if (closed == closed_) return {};
  closed_ = closed;
  if (closed) {
    if (ctx_ == Context::Game) {
      paused_ = true;
      return {Action::SaveState, Action::PauseGame, Action::PanelOff, Action::PowerSave};
    }
    touch_ = true;
    return {Action::EnterTouchMode};
  }
  if (paused_) {
    paused_ = false;
    return {Action::PowerRestore, Action::PanelOn, Action::ResumeGame};
  }
  if (touch_) {
    touch_ = false;
    return {Action::ExitTouchMode};
  }
  return {};
}

std::vector<Action> SwivelFsm::on_context(Context c) {
  if (c == ctx_) return {};
  ctx_ = c;
  std::vector<Action> out;
  if (c == Context::Game) {
    if (!closed_) return out;
    if (touch_) {
      touch_ = false;
      out.push_back(Action::ExitTouchMode);
    }
    paused_ = true;
    out.insert(out.end(), {Action::PauseGame, Action::PanelOff, Action::PowerSave});
    return out;
  }
  if (paused_) {  // game ended while the swivel is down
    paused_ = false;
    touch_ = true;
    return {Action::PowerRestore, Action::PanelOn, Action::EnterTouchMode};
  }
  return out;
}

// ---- evdev -------------------------------------------------------------------
int switch_code_from_name(const std::string& name) {
  auto n = to_lower(name);
  if (n == "lid") return SW_LID;
  if (n == "tablet") return SW_TABLET_MODE;
  return -1;
}

EvdevSwitch::EvdevSwitch(int fd, int code, int closed_value, bool owns_fd)
    : fd_(fd), code_(code), closed_value_(closed_value), owns_(owns_fd) {
  int fl = fcntl(fd_, F_GETFL);
  if (fl >= 0) fcntl(fd_, F_SETFL, fl | O_NONBLOCK);
}

EvdevSwitch::~EvdevSwitch() {
  if (owns_ && fd_ >= 0) close(fd_);
}

std::unique_ptr<EvdevSwitch> EvdevSwitch::open_path(const std::string& path, int code, int closed_value,
                                                     std::string* err) {
  int fd = open(path.c_str(), O_RDONLY | O_CLOEXEC | O_NONBLOCK);
  if (fd < 0) {
    if (err) *err = path + ": " + std::strerror(errno);
    return nullptr;
  }
  return std::make_unique<EvdevSwitch>(fd, code, closed_value, true);
}

std::string EvdevSwitch::find_device(const std::string& dev_dir, int code) {
  DIR* d = opendir(dev_dir.c_str());
  if (!d) return "";
  std::string found;
  while (auto* e = readdir(d)) {
    if (std::strncmp(e->d_name, "event", 5) != 0) continue;
    std::string p = dev_dir + "/" + e->d_name;
    int fd = open(p.c_str(), O_RDONLY | O_CLOEXEC | O_NONBLOCK);
    if (fd < 0) continue;
    unsigned long bits[(SW_MAX / (8 * sizeof(long))) + 1] = {0};
    bool has = ioctl(fd, EVIOCGBIT(EV_SW, sizeof bits), bits) >= 0 &&
               (bits[code / (8 * sizeof(long))] >> (code % (8 * sizeof(long)))) & 1;
    close(fd);
    if (has) {
      found = p;
      break;
    }
  }
  closedir(d);
  return found;
}

std::optional<bool> EvdevSwitch::initial_state() const {
  unsigned long sw[(SW_MAX / (8 * sizeof(long))) + 1] = {0};
  if (ioctl(fd_, EVIOCGSW(sizeof sw), sw) < 0) return std::nullopt;
  int v = (sw[code_ / (8 * sizeof(long))] >> (code_ % (8 * sizeof(long)))) & 1;
  return v == closed_value_;
}

std::vector<bool> EvdevSwitch::read_events(bool* eof) {
  std::vector<bool> out;
  if (eof) *eof = false;
  char buf[sizeof(struct input_event) * 16];
  for (;;) {
    ssize_t n = read(fd_, buf, sizeof buf);
    if (n < 0) {
      if (errno == EINTR) continue;
      if (errno != EAGAIN && eof) *eof = true;
      break;
    }
    if (n == 0) {
      if (eof) *eof = true;
      break;
    }
    partial_.append(buf, static_cast<size_t>(n));
    while (partial_.size() >= sizeof(struct input_event)) {
      struct input_event ev;
      std::memcpy(&ev, partial_.data(), sizeof ev);
      partial_.erase(0, sizeof ev);
      if (ev.type == EV_SW && ev.code == code_) out.push_back(ev.value == closed_value_);
    }
  }
  return out;
}

}  // namespace rotate
