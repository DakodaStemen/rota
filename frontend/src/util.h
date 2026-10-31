#pragma once
#include <optional>
#include <string>
#include <vector>

namespace rotate {

std::string trim(const std::string& s);
std::string to_lower(std::string s);
std::vector<std::string> split_ws(const std::string& s);
bool ends_with(const std::string& s, const std::string& suffix);
std::optional<std::string> read_file(const std::string& path);   // whole file, untrimmed
std::optional<std::string> read_line(const std::string& path);   // trimmed first line
bool write_file(const std::string& path, const std::string& data);
bool make_dirs(const std::string& path);
std::string env_or(const char* name, const std::string& def);

struct RunResult {
  int exit_code = -1;      // -1: could not start / killed by signal / timeout
  bool timed_out = false;
  std::string output;      // stdout+stderr
};
// fork/exec argv with extra "KEY=VALUE" env entries; no shell involved.
RunResult run_capture(const std::vector<std::string>& argv,
                      const std::vector<std::string>& extra_env, int timeout_ms);

}  // namespace rotate
