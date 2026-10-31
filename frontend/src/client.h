#pragma once
#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

namespace rotate {

// Frontend side of the rotate-swiveld protocol. A reader thread connects (retrying every few seconds when
// the daemon is absent), and hands each received line to `on_line` on that thread. Blocks in poll(): no
// wakeups while idle.
class SwivelClient {
 public:
  using LineHandler = std::function<void(const std::string&)>;
  SwivelClient(std::string socket_path, LineHandler on_line, int retry_ms = 3000)
      : path_(std::move(socket_path)), on_line_(std::move(on_line)), retry_ms_(retry_ms) {}
  ~SwivelClient() { stop(); }
  void start();
  void stop();
  bool send(const std::string& line);  // false when not connected

 private:
  void run();
  std::string path_;
  LineHandler on_line_;
  int retry_ms_;
  std::thread th_;
  int stop_fd_ = -1;   // eventfd: wakes the reader on stop()
  std::mutex mu_;
  int fd_ = -1;
};

}  // namespace rotate
