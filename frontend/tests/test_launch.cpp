// RetroArch command line, generated append config, spawn/wait with a fake retroarch.
#include "check.h"
#include "retroarch.h"

using namespace rotate;

int main() {
  TempDir t;
  Config c;
  c.state_dir = t / "state";
  c.cores_dir = "/cores";
  c.retroarch_cfg = "/etc/retroarch.cfg";
  SystemDef sys{"gba", "GBA", "mgba", {"gba"}};
  Game g{"gba", "Advance Wars", "/roms/gba/Advance Wars (USA).gba"};

  LaunchSpec s = build_launch(c, sys, g);
  std::vector<std::string> want = {c.retroarch_bin, "-L", "/cores/mgba_libretro.so", "--appendconfig",
                                   t / "state/retroarch-rotate.cfg", "--config", "/etc/retroarch.cfg",
                                   "/roms/gba/Advance Wars (USA).gba"};
  CHECK(s.argv == want);
  CHECK(s.append_config_text.find("savestate_auto_save = \"true\"") != std::string::npos);
  CHECK(s.append_config_text.find("savestate_auto_load = \"true\"") != std::string::npos);
  CHECK(s.append_config_text.find("network_cmd_enable = \"true\"") != std::string::npos);
  CHECK(s.append_config_text.find("network_cmd_port = \"55355\"") != std::string::npos);

  c.resume_on_wake = false;
  c.retroarch_cfg = "";
  c.cmd_port = 4000;
  LaunchSpec s2 = build_launch(c, sys, g);
  CHECK(s2.append_config_text.find("savestate_auto") == std::string::npos);
  CHECK(s2.append_config_text.find("\"4000\"") != std::string::npos);
  CHECK_EQ(s2.argv.size(), size_t(6));  // no --config

  // fake retroarch records its argv and exits with 7
  t.put("fake-ra", "#!/bin/sh\nfor a in \"$@\"; do echo \"$a\"; done > " + t / "argv.txt" + "\nexit 7\n");
  chmod((t / "fake-ra").c_str(), 0755);
  c.retroarch_bin = t / "fake-ra";
  c.resume_on_wake = true;
  LaunchSpec s3 = build_launch(c, sys, g);
  CHECK(prepare_launch(s3));
  CHECK_EQ(t.get("state/retroarch-rotate.cfg"), s3.append_config_text);
  pid_t pid = spawn(s3);
  CHECK(pid > 0);
  CHECK_EQ(wait_child(pid), 7);
  CHECK(t.get("argv.txt").find("-L\n/cores/mgba_libretro.so\n--appendconfig\n") != std::string::npos);
  CHECK(t.get("argv.txt").find("Advance Wars (USA).gba\n") != std::string::npos);

  c.retroarch_bin = t / "does-not-exist";
  pid = spawn(build_launch(c, sys, g));
  CHECK_EQ(wait_child(pid), 127);  // exec failure surfaces as 127; the frontend shows a status line
  TEST_MAIN_END();
}
