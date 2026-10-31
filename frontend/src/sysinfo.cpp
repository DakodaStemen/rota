#include "sysinfo.h"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <vector>

#include "util.h"

namespace fs = std::filesystem;

namespace rotate {

static std::vector<fs::path> list_dir(const fs::path& d) {
  std::vector<fs::path> v;
  std::error_code ec;
  for (fs::directory_iterator it(d, ec), end; !ec && it != end; it.increment(ec)) v.push_back(it->path());
  std::sort(v.begin(), v.end());
  return v;
}

Stats read_stats(const std::string& root) {
  Stats s;
  for (const auto& d : list_dir(root + "/class/power_supply")) {
    auto type = read_line((d / "type").string());
    if (!type || to_lower(*type) != "battery") continue;
    if (auto cap = read_line((d / "capacity").string())) {
      s.battery_pct = std::atoi(cap->c_str());
      break;
    }
  }
  std::string cf = root + "/devices/system/cpu/cpu0/cpufreq";
  if (auto f = read_line(cf + "/scaling_cur_freq")) s.cpu_khz = std::atol(f->c_str());
  if (auto g = read_line(cf + "/scaling_governor")) s.governor = *g;
  for (const auto& d : list_dir(root + "/class/thermal")) {
    if (d.filename().string().rfind("thermal_zone", 0) != 0) continue;
    if (auto t = read_line((d / "temp").string())) {
      int mc = std::atoi(t->c_str());
      if (s.temp_mc == INT_MIN || mc > s.temp_mc) s.temp_mc = mc;
    }
  }
  return s;
}

std::string Backlight::find() const {
  for (const auto& d : list_dir(root_ + "/class/backlight")) {
    if (fs::exists(d / "brightness")) return (d / "brightness").string();
  }
  return "";
}

bool Backlight::off() {
  if (off_) return true;
  std::string p = find();
  if (p.empty()) return false;
  auto cur = read_line(p);
  if (!cur) return false;
  if (!write_file(p, "0\n")) return false;
  path_ = p;
  saved_ = *cur;
  off_ = true;
  return true;
}

bool Backlight::on() {
  if (!off_) return true;
  bool ok = write_file(path_, saved_ + "\n");
  off_ = false;
  return ok;
}

}  // namespace rotate
