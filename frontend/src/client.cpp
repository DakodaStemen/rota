#include "client.h"

#include <poll.h>
#include <sys/eventfd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <cstring>

namespace rotate {

void SwivelClient::start() {
  stop_fd_ = eventfd(0, EFD_CLOEXEC);
  th_ = std::thread([this] { run(); });
}

void SwivelClient::stop() {
  if (!th_.joinable()) return;
  uint64_t one = 1;
  (void)!write(stop_fd_, &one, sizeof one);
  th_.join();
  close(stop_fd_);
  stop_fd_ = -1;
}

bool SwivelClient::send(const std::string& line) {
  std::lock_guard<std::mutex> g(mu_);
  if (fd_ < 0) return false;
  std::string l = line + "\n";
  return ::send(fd_, l.data(), l.size(), MSG_NOSIGNAL) == static_cast<ssize_t>(l.size());
}

void SwivelClient::run() {
  for (;;) {
    int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    sockaddr_un a{};
    a.sun_family = AF_UNIX;
    std::strncpy(a.sun_path, path_.c_str(), sizeof a.sun_path - 1);
    if (fd >= 0 && connect(fd, reinterpret_cast<sockaddr*>(&a), sizeof a) == 0) {
      {
        std::lock_guard<std::mutex> g(mu_);
        fd_ = fd;
      }
      std::string buf;
      bool stop = false;
      for (;;) {
        pollfd p[2] = {{fd, POLLIN, 0}, {stop_fd_, POLLIN, 0}};
        if (poll(p, 2, -1) < 0) continue;
        if (p[1].revents) { stop = true; break; }
        char tmp[256];
        ssize_t n = recv(fd, tmp, sizeof tmp, 0);
        if (n <= 0) break;
        buf.append(tmp, static_cast<size_t>(n));
        size_t nl;
        while ((nl = buf.find('\n')) != std::string::npos) {
          on_line_(buf.substr(0, nl));
          buf.erase(0, nl + 1);
        }
      }
      {
        std::lock_guard<std::mutex> g(mu_);
        fd_ = -1;
      }
      close(fd);
      if (stop) return;
    } else if (fd >= 0) {
      close(fd);
    }
    pollfd s = {stop_fd_, POLLIN, 0};
    if (poll(&s, 1, retry_ms_) > 0) return;
  }
}

}  // namespace rotate
