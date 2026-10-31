#pragma once
#include <climits>
#include <string>

namespace rotate {

struct Stats {
  int battery_pct = -1;          // -1: unknown
  long cpu_khz = -1;             // cpu0 scaling_cur_freq, -1 unknown
  int temp_mc = INT_MIN;         // hottest thermal zone, millidegrees C; INT_MIN unknown
  std::string governor;          // cpu0 scaling_governor, empty unknown
};

Stats read_stats(const std::string& sysfs_root);

// Panel blanking through <sysfs_root>/class/backlight/*/brightness. Restores the saved value on on().
class Backlight {
 public:
  explicit Backlight(std::string sysfs_root) : root_(std::move(sysfs_root)) {}
  bool off();   // remember brightness, write 0. false if no backlight is writable
  bool on();    // write remembered brightness back
  bool is_off() const { return off_; }

 private:
  std::string find() const;
  std::string root_;
  std::string path_;
  std::string saved_;
  bool off_ = false;
};

}  // namespace rotate
