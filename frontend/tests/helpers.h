#pragma once
// Fake RetroArch network-command endpoint and small wait helpers shared by the swivel tests.
#include <arpa/inet.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

#include "check.h"

class FakeRetroArch {
 public:
  // status_reply: what GET_STATUS answers ("" = stay silent like a RetroArch without network_cmd_enable)
  explicit FakeRetroArch(std::string status_reply) : reply_(std::move(status_reply)) {
    fd_ = socket(AF_INET, SOCK_DGRAM, 0);
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    a.sin_port = 0;
    bind(fd_, reinterpret_cast<sockaddr*>(&a), sizeof a);
    socklen_t l = sizeof a;
    getsockname(fd_, reinterpret_cast<sockaddr*>(&a), &l);
    port_ = ntohs(a.sin_port);
    th_ = std::thread([this] {
      while (!stop_) {
        pollfd p = {fd_, POLLIN, 0};
        if (poll(&p, 1, 20) <= 0) continue;
        char buf[256];
        sockaddr_in from{};
        socklen_t fl = sizeof from;
        ssize_t n = recvfrom(fd_, buf, sizeof buf, 0, reinterpret_cast<sockaddr*>(&from), &fl);
        if (n <= 0) continue;
        std::string cmd(buf, static_cast<size_t>(n));
        {
          std::lock_guard<std::mutex> g(mu_);
          got_.push_back(cmd);
        }
        if (cmd == "GET_STATUS" && !reply_.empty())
          sendto(fd_, reply_.data(), reply_.size(), 0, reinterpret_cast<sockaddr*>(&from), fl);
      }
    });
  }
  ~FakeRetroArch() {
    stop_ = true;
    th_.join();
    close(fd_);
  }
  int port() const { return port_; }
  std::vector<std::string> got() {
    std::lock_guard<std::mutex> g(mu_);
    return got_;
  }

 private:
  int fd_, port_;
  std::string reply_;
  std::atomic<bool> stop_{false};
  std::thread th_;
  std::mutex mu_;
  std::vector<std::string> got_;
};

inline bool wait_until(const std::function<bool()>& pred, int ms = 3000) {
  auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
  while (std::chrono::steady_clock::now() < end) {
    if (pred()) return true;
    usleep(10 * 1000);
  }
  return pred();
}

class Lines {
 public:
  void add(const std::string& l) {
    std::lock_guard<std::mutex> g(mu_);
    v_.push_back(l);
  }
  bool has(const std::string& l) {
    std::lock_guard<std::mutex> g(mu_);
    for (auto& x : v_) if (x == l) return true;
    return false;
  }
  // index of first occurrence at or after `from`, or -1
  int find(const std::string& l, int from = 0) {
    std::lock_guard<std::mutex> g(mu_);
    for (size_t i = static_cast<size_t>(from); i < v_.size(); ++i) if (v_[i] == l) return static_cast<int>(i);
    return -1;
  }
  size_t size() {
    std::lock_guard<std::mutex> g(mu_);
    return v_.size();
  }
 private:
  std::mutex mu_;
  std::vector<std::string> v_;
};
