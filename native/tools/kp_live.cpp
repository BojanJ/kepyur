// kp_live: kepyur's radio (scan / watch / stream test) on the desktop with the adapter plugged in.
//   kp_live --scan [--frames]               scan, then watch the strongest pilot
//   kp_live --watch 5769 [--frames]         watch one frequency (MHz)
//   kp_live --test 1 60                     stream test: rate_sel (1 = 40 MS/s, 2 = 20 MS/s), seconds
//   ... --frames | ffplay -f rawvideo -pixel_format rgb0 -video_size 720x480 -framerate 60 -fflags nobuffer -i -
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>

#include "log.h"
#include "radio.h"

using namespace kp;
static std::atomic<bool> g_stop{false};

int main(int argc, char **argv) {
  std::string mode = "scan";
  double mhz = 5769, secs = 60;
  int rate = 1;
  bool frames = false;
  for (int i = 1; i < argc; i++) {
    const std::string k = argv[i];
    if (k == "--scan") mode = "scan";
    else if (k == "--watch" && i + 1 < argc) { mode = "watch"; mhz = std::atof(argv[++i]); }
    else if (k == "--test" && i + 2 < argc) { mode = "test"; rate = std::atoi(argv[++i]); secs = std::atof(argv[++i]); }
    else if (k == "--frames") frames = true;
    else if (k == "-v") set_log_debug(true);
    else { std::fprintf(stderr, "usage: kp_live [--scan | --watch MHZ | --test RATE_SEL SECONDS] [--frames] [-v]\n"); return 2; }
  }
  std::signal(SIGINT, [](int) { g_stop = true; });
  std::signal(SIGTERM, [](int) { g_stop = true; });
  std::signal(SIGPIPE, [](int) { g_stop = true; });
  Radio radio([&](const Frame &f) {
    if (frames) {
      std::fwrite(f.data, 1, static_cast<size_t>(f.width) * f.height * 4, stdout);
      std::fflush(stdout);
    }
  });
  try {
    radio.open(-1);
  } catch (const std::exception &e) {
    std::fprintf(stderr, "open failed: %s\n", e.what());
    return 1;
  }
  if (mode == "scan") radio.scan();
  else if (mode == "watch") radio.watch(mhz * 1e6);
  else radio.stream_test(rate, secs);
  const auto t0 = std::chrono::steady_clock::now();
  while (!g_stop) {
    std::this_thread::sleep_for(std::chrono::seconds(1));
    const std::string st = radio.status_json();
    std::fprintf(stderr, "%s\n", st.c_str());
    if (mode == "test" && st.find("\"done\":true") != std::string::npos) break;
    if (mode == "test" && std::chrono::steady_clock::now() - t0 > std::chrono::duration<double>(secs + 30)) break;
  }
  radio.close();
  return 0;
}
