// kp_replay: run kepyur's scanner DSP and video decoder over a recording (.cs16 + .json from wsdr_probe,
// or .sigmf-data + .sigmf-meta) and print lock numbers. The bench for lock robustness.
//
//   kp_replay REC.cs16 [--offset HZ] [--start S] [--seconds S] [--cnr DB] [--frames] [--woven] [-v]
//   kp_replay REC.cs16 --frames | ffplay -f rawvideo -pixel_format gray -video_size 720x480 -i -
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <random>
#include <string>
#include <vector>

#include "log.h"
#include "spectrum.h"
#include "video.h"

using namespace kp;

static double json_number(const std::string &txt, const char *key) {
  const auto p = txt.find(std::string("\"") + key + "\"");
  if (p == std::string::npos) return NAN;
  const auto c = txt.find(':', p);
  return std::atof(txt.c_str() + c + 1);
}

int main(int argc, char **argv) {
  std::string file;
  double offset = NAN, start = 0, seconds = 0, cnr = 0, fs = 40e6;
  bool frames = false, woven = false;
  for (int i = 1; i < argc; i++) {
    const std::string k = argv[i];
    auto v = [&] { return i + 1 < argc ? argv[++i] : (std::fprintf(stderr, "missing value\n"), std::exit(2), ""); };
    if (k == "--offset") offset = std::atof(v());
    else if (k == "--start") start = std::atof(v());
    else if (k == "--seconds") seconds = std::atof(v());
    else if (k == "--cnr") cnr = std::atof(v());
    else if (k == "--fs") fs = std::atof(v());
    else if (k == "--frames") frames = true;
    else if (k == "--woven") woven = true;
    else if (k == "-v") set_log_debug(true);
    else if (k[0] != '-') file = k;
    else {
      std::fprintf(stderr, "usage: kp_replay REC.cs16 [--offset HZ] [--start S] [--seconds S] [--cnr DB] [--frames] [--woven] [-v]\n");
      return 2;
    }
  }
  if (file.empty()) return 2;
  double centre = NAN;
  {
    const auto dot = file.rfind('.');
    const std::string base = file.substr(0, dot), ext = file.substr(dot);
    std::ifstream m(ext == ".sigmf-data" ? base + ".sigmf-meta" : base + ".json");
    if (m) {
      const std::string txt((std::istreambuf_iterator<char>(m)), {});
      double r = json_number(txt, ext == ".sigmf-data" ? "core:sample_rate" : "sample_rate");
      if (!std::isnan(r)) fs = r;
      if (ext == ".sigmf-data") centre = json_number(txt, "core:frequency");
      else {
        const double ch = json_number(txt, "channel") + (fs >= 40e6 ? 2 : 0);  // primary -> centre at BW40
        centre = (5000 + 5 * ch) * 1e6;
      }
    }
  }
  FILE *f = std::fopen(file.c_str(), "rb");
  if (!f) {
    std::perror(file.c_str());
    return 1;
  }
  std::fseek(f, static_cast<long>(start * fs) * 4, SEEK_SET);
  const size_t total = seconds > 0 ? static_cast<size_t>(seconds * fs) : SIZE_MAX;

  std::vector<int16_t> raw(2 * 65536);
  std::vector<cf> x(65536);
  auto read_block = [&](size_t want) -> size_t {
    const size_t n = std::fread(raw.data(), 4, std::min<size_t>(want, 65536), f);
    for (size_t i = 0; i < n; i++) x[i] = cf(raw[2 * i] / 16.f, raw[2 * i + 1] / 16.f);
    return n;
  };

  // ---- scanner view of the first 25 ms: humps + sync check ----
  {
    const long pos = std::ftell(f);
    const size_t want = static_cast<size_t>(0.1 * fs);
    std::vector<cf> buf;
    while (buf.size() < want) {
      const size_t n = read_block(want - buf.size());
      if (!n) break;
      buf.insert(buf.end(), x.begin(), x.begin() + n);
    }
    Psd psd(1024);
    psd.add(buf.data(), std::min(buf.size(), static_cast<size_t>(0.025 * fs)));
    const auto humps = find_humps(psd.db(), fs, 0.42 * fs);
    std::fprintf(stderr, "scan: %zu hump(s) in +-%.0f MHz around %.1f MHz\n", humps.size(), 0.42 * fs / 1e6, centre / 1e6);
    for (const auto &h : humps) {
      const auto vc = check_video(buf.data(), buf.size(), fs, h.offset_hz);
      const double mhz = (centre + h.offset_hz) / 1e6;
      const auto *ch = nearest_channel(mhz);
      std::fprintf(stderr, "  %+.2f MHz (%.1f MHz %s) width %.1f MHz snr %.1f dB -> %s %s lock %.2f dev %.1f MHz\n",
                   h.offset_hz / 1e6, mhz, ch ? ch->name : "--", h.width_hz / 1e6, h.snr_db,
                   vc.video ? "VIDEO" : "no video", vc.std_name, vc.lock, vc.dev_mhz);
      if (std::isnan(offset) && vc.video) offset = h.offset_hz;
    }
    std::fseek(f, pos, SEEK_SET);
  }
  if (std::isnan(offset)) {
    std::fprintf(stderr, "no video found; pass --offset\n");
    offset = -1e6;
  }

  // ---- decode ----
  DecoderOpts o;
  o.fields = !woven;
  int fw = 0, fh = 0;
  FrontEnd fe(fs, offset, [&](const Frame &fr) {
    fw = fr.width;
    fh = fr.height;
    if (frames) std::fwrite(fr.data, 1, static_cast<size_t>(fr.width) * fr.height, stdout);
  }, o);
  std::mt19937 rng(1);
  std::normal_distribution<float> nd(0.f, 1.f);
  float noise_sd = 0;
  if (cnr > 0) {  // add white noise for this carrier-to-noise ratio in an 18 MHz video channel
    double p = 0;
    const size_t n = read_block(1 << 16);
    for (size_t i = 0; i < n; i++) p += std::norm(x[i]);
    p /= std::max<size_t>(1, n);
    // p is ~all signal (the recordings are ~30 dB above the floor); noise spreads over the whole fs
    const double noise_total = p / std::pow(10, cnr / 10) * (fs / 18e6);
    noise_sd = static_cast<float>(std::sqrt(noise_total / 2));
    std::fseek(f, static_cast<long>(start * fs) * 4, SEEK_SET);
    std::fprintf(stderr, "adding noise: sd %.1f per component (input power %.1f)\n", noise_sd, p);
  }
  size_t done = 0, next_report = static_cast<size_t>(fs);
  uint64_t last_frames = 0;
  const auto t0 = std::chrono::steady_clock::now();
  while (done < total) {
    const size_t n = read_block(std::min<size_t>(65536, total - done));
    if (!n) break;
    if (noise_sd > 0)
      for (size_t i = 0; i < n; i++) x[i] += cf(noise_sd * nd(rng), noise_sd * nd(rng));
    fe.feed(x.data(), n);
    done += n;
    if (done >= next_report) {
      next_report += static_cast<size_t>(fs);
      const auto s = fe.stats();
      std::fprintf(stderr, "t=%5.1fs %s %s lock(recent) %.2f lines %llu/%llu fields %llu frames +%llu acq %llu lost %llu tip %.2f blank %.2f\n",
                   done / fs, s.acquiring ? "ACQ " : "LOCK", s.std_name, s.lock_recent, (unsigned long long)s.lines_locked,
                   (unsigned long long)s.lines, (unsigned long long)s.fields, (unsigned long long)(s.frames - last_frames),
                   (unsigned long long)s.acquisitions, (unsigned long long)s.losses, s.tip, s.blank);
      last_frames = s.frames;
    }
  }
  const double dt = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
  const auto s = fe.stats();
  std::fprintf(stderr, "done: %.2f s of IQ in %.2f s (%.1fx realtime), offset %+.3f MHz, frames %llu (%dx%d), "
               "acquisitions %llu, losses %llu, lines locked %.1f%%\n",
               done / fs, dt, done / fs / dt, offset / 1e6, (unsigned long long)s.frames, fw, fh,
               (unsigned long long)s.acquisitions, (unsigned long long)s.losses,
               s.lines ? 100.0 * s.lines_locked / s.lines : 0.0);
  return 0;
}
