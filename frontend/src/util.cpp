#include "util.h"

#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace rotate {

std::string trim(const std::string& s) {
  size_t a = 0, b = s.size();
  while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
  while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
  return s.substr(a, b - a);
}

std::string to_lower(std::string s) {
  for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return s;
}

std::vector<std::string> split_ws(const std::string& s) {
  std::vector<std::string> out;
  std::istringstream is(s);
  std::string tok;
  while (is >> tok) out.push_back(tok);
  return out;
}

bool ends_with(const std::string& s, const std::string& suffix) {
  return s.size() >= suffix.size() && s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

std::optional<std::string> read_file(const std::string& path) {
  std::ifstream f(path, std::ios::binary);
  if (!f) return std::nullopt;
  std::ostringstream ss;
  ss << f.rdbuf();
  return ss.str();
}

std::optional<std::string> read_line(const std::string& path) {
  std::ifstream f(path);
  if (!f) return std::nullopt;
  std::string line;
  std::getline(f, line);
  return trim(line);
}

bool write_file(const std::string& path, const std::string& data) {
  std::ofstream f(path, std::ios::binary | std::ios::trunc);
  if (!f) return false;
  f << data;
  f.flush();
  return static_cast<bool>(f);
}

bool make_dirs(const std::string& path) {
  std::string cur;
  for (size_t i = 0; i <= path.size(); ++i) {
    if (i == path.size() || path[i] == '/') {
      if (!cur.empty() && mkdir(cur.c_str(), 0755) != 0) {
        struct stat st;
        if (stat(cur.c_str(), &st) != 0 || !S_ISDIR(st.st_mode)) return false;
      }
    }
    if (i < path.size()) cur += path[i];
  }
  return true;
}

std::string env_or(const char* name, const std::string& def) {
  const char* v = std::getenv(name);
  return (v && *v) ? v : def;
}

RunResult run_capture(const std::vector<std::string>& argv,
                      const std::vector<std::string>& extra_env, int timeout_ms) {
  RunResult r;
  if (argv.empty()) return r;
  int pfd[2];
  if (pipe2(pfd, O_CLOEXEC) != 0) return r;
  pid_t pid = fork();
  if (pid < 0) {
    close(pfd[0]);
    close(pfd[1]);
    return r;
  }
  if (pid == 0) {
    dup2(pfd[1], 1);
    dup2(pfd[1], 2);
    for (const auto& e : extra_env) putenv(const_cast<char*>(e.c_str()));
    std::vector<char*> av;
    for (const auto& a : argv) av.push_back(const_cast<char*>(a.c_str()));
    av.push_back(nullptr);
    execvp(av[0], av.data());
    _exit(127);
  }
  close(pfd[1]);
  auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
  char buf[512];
  for (;;) {
    auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now()).count();
    if (left <= 0) {
      r.timed_out = true;
      kill(pid, SIGKILL);
      break;
    }
    struct pollfd p = {pfd[0], POLLIN, 0};
    int n = poll(&p, 1, static_cast<int>(left));
    if (n < 0) continue;
    if (n == 0) continue;
    ssize_t got = read(pfd[0], buf, sizeof buf);
    if (got <= 0) break;
    r.output.append(buf, static_cast<size_t>(got));
  }
  close(pfd[0]);
  int status = 0;
  waitpid(pid, &status, 0);
  if (!r.timed_out && WIFEXITED(status)) r.exit_code = WEXITSTATUS(status);
  return r;
}

}  // namespace rotate
