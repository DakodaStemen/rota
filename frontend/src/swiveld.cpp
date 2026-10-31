#include "swiveld.h"

#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

#include <cerrno>
#include <cstdlib>
#include <cstring>

#include "util.h"

namespace rotate {

Swiveld::~Swiveld() {
  for (auto& c : clients_) close(c.first);
  if (listen_fd_ >= 0) {
    close(listen_fd_);
    unlink(sock_path_.c_str());
  }
}

bool Swiveld::listen_on(const std::string& path, std::string* err) {
  sockaddr_un addr{};
  if (path.size() >= sizeof addr.sun_path) {
    if (err) *err = "socket path too long";
    return false;
  }
  auto slash = path.rfind('/');
  if (slash != std::string::npos && slash > 0) make_dirs(path.substr(0, slash));
  int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
  if (fd < 0) {
    if (err) *err = std::strerror(errno);
    return false;
  }
  addr.sun_family = AF_UNIX;
  std::strcpy(addr.sun_path, path.c_str());
  unlink(path.c_str());
  if (bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof addr) != 0 || listen(fd, 4) != 0) {
    if (err) *err = path + ": " + std::strerror(errno);
    close(fd);
    return false;
  }
  chmod(path.c_str(), 0666);  // the frontend may not run as the same user as the daemon
  listen_fd_ = fd;
  sock_path_ = path;
  return true;
}

static void send_line(int fd, const std::string& line) {
  std::string l = line + "\n";
  send(fd, l.data(), l.size(), MSG_NOSIGNAL | MSG_DONTWAIT);
}

void Swiveld::greet(int fd) {
  send_line(fd, std::string("SWIVEL ") + (!state_known_ ? "unknown" : fsm_.closed() ? "closed" : "open"));
  send_line(fd, std::string("MODE ") + (fsm_.context() == Context::Game ? "game" : "menu"));
  send_line(fd, std::string("TOUCH ") + (fsm_.touch_mode() ? "on" : "off"));
}

void Swiveld::broadcast(const std::string& line) {
  for (auto& c : clients_) send_line(c.first, line);
}

void Swiveld::apply(const std::vector<Action>& acts) {
  for (Action a : acts) {
    if (a == Action::EnterTouchMode) broadcast("TOUCH on");
    else if (a == Action::ExitTouchMode) broadcast("TOUCH off");
    else sink_.run(a, game_pid_);
  }
}

void Swiveld::handle_switch(bool closed) {
  state_known_ = true;
  auto acts = fsm_.on_switch(closed);
  broadcast(std::string("SWIVEL ") + (closed ? "closed" : "open"));
  apply(acts);
}

void Swiveld::on_line(int fd, const std::string& line) {
  auto parts = split_ws(line);
  if (parts.empty()) return;
  if (parts[0] == "GAME" && parts.size() >= 2) {
    game_pid_ = static_cast<pid_t>(std::atol(parts[1].c_str()));
    apply(fsm_.on_context(Context::Game));
  } else if (parts[0] == "MENU") {
    apply(fsm_.on_context(Context::Menu));
    game_pid_ = 0;
  } else if (parts[0] == "GET") {
    greet(fd);
  }
}

void Swiveld::drop(int fd) {
  clients_.erase(fd);
  close(fd);
  if (game_pid_ != 0 && clients_.empty()) {  // the frontend died mid-game; treat as game over
    apply(fsm_.on_context(Context::Menu));
    game_pid_ = 0;
  }
}

int Swiveld::poll_once(int timeout_ms, int extra_fd, bool* extra_ready) {
  std::vector<pollfd> pfds;
  if (listen_fd_ >= 0) pfds.push_back({listen_fd_, POLLIN, 0});
  for (auto& c : clients_) pfds.push_back({c.first, POLLIN, 0});
  size_t extra_idx = pfds.size();
  if (extra_fd >= 0) pfds.push_back({extra_fd, POLLIN, 0});
  if (extra_ready) *extra_ready = false;
  int n = poll(pfds.data(), pfds.size(), timeout_ms);
  if (n <= 0) return 0;
  int handled = 0;
  for (size_t i = 0; i < pfds.size(); ++i) {
    if (!pfds[i].revents) continue;
    ++handled;
    int fd = pfds[i].fd;
    if (extra_fd >= 0 && i == extra_idx) {
      if (extra_ready) *extra_ready = true;
    } else if (fd == listen_fd_) {
      int cfd = accept4(listen_fd_, nullptr, nullptr, SOCK_CLOEXEC | SOCK_NONBLOCK);
      if (cfd >= 0) {
        clients_[cfd] = Client{};
        greet(cfd);
      }
    } else {
      char buf[256];
      ssize_t r = recv(fd, buf, sizeof buf, 0);
      if (r == 0 || (r < 0 && errno != EAGAIN && errno != EINTR)) {
        drop(fd);
        continue;
      }
      if (r > 0) {
        auto& in = clients_[fd].inbuf;
        in.append(buf, static_cast<size_t>(r));
        size_t nl;
        while ((nl = in.find('\n')) != std::string::npos) {
          std::string line = in.substr(0, nl);
          in.erase(0, nl + 1);
          on_line(fd, line);
        }
      }
    }
  }
  return handled;
}

}  // namespace rotate
