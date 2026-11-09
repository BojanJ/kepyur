// A stand-in for wsdr::Receiver that plays a recording back as a live, tunable 8812BU (kp_sim only).
// The recording's VTX stays at its real frequency: tuning elsewhere moves it in the band, and a step far
// from it shows only noise. Bank pacing is real time. Env: KP_SIM_FILE (.cs16, 40 MS/s, centre 5770 MHz).
#include <chrono>
#include <cmath>
#include <complex>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <thread>
#include <vector>

#include "receiver.h"
#include "stream88xx.h"

namespace wsdr {

const char *rx_state_name(RxState s) {
  switch (s) {
  case RxState::Closed: return "closed";
  case RxState::Opening: return "opening";
  case RxState::Starting: return "starting";
  case RxState::Streaming: return "streaming";
  case RxState::Lost: return "lost";
  case RxState::Error: return "error";
  }
  return "?";
}

namespace {
struct Sim {
  std::vector<int16_t> rec;  // I,Q (value << 4)
  size_t pos = 0;
  double rec_centre = 5770e6;
};
Sim *g_sim = nullptr;
}  // namespace

Receiver::Receiver(Logger_t log) : _log(std::move(log)) {}
Receiver::~Receiver() { close(); }

void Receiver::set_state(RxState s, const std::string &err) {
  std::lock_guard<std::mutex> l(_m);
  _st.state = s;
  _st.error = err;
}

void Receiver::open(uint16_t pid, const RxSettings &initial, int, const std::string &, int) {
  const char *f = std::getenv("KP_SIM_FILE");
  if (!f) throw std::runtime_error("KP_SIM_FILE not set");
  FILE *fp = std::fopen(f, "rb");
  if (!fp) throw std::runtime_error(std::string("cannot open ") + f);
  g_sim = new Sim;
  std::fseek(fp, 0, SEEK_END);
  const long bytes = std::min<long>(std::ftell(fp), 40000000L * 4 * 3);  // up to 3 s
  std::fseek(fp, 0, SEEK_SET);
  g_sim->rec.resize(bytes / 2);
  if (std::fread(g_sim->rec.data(), 2, g_sim->rec.size(), fp) != g_sim->rec.size()) throw std::runtime_error("short read");
  std::fclose(fp);
  _set = initial;
  _caps = {};
  _caps.pid = pid;
  _caps.superspeed = std::getenv("KP_SIM_USB2") == nullptr;
  _caps.chip = "SIM (recording)";
  _caps.min_rate_sel = _caps.superspeed ? 1 : 4;
  _open = true;
  set_state(RxState::Starting);
}

void Receiver::close() {
  stop();
  _open = false;
  delete g_sim;
  g_sim = nullptr;
}

void Receiver::start(BankSink sink) {
  _sink = std::move(sink);
  _active = true;
  _worker = std::thread([this] { worker(); });
}

void Receiver::stop() {
  _active = false;
  if (_worker.joinable()) _worker.join();
}

void Receiver::set(const RxSettings &s) {
  std::lock_guard<std::mutex> l(_m);
  _set = s;
  _restart = true;
}

RxSettings Receiver::settings() const {
  std::lock_guard<std::mutex> l(_m);
  return _set;
}

RxStatus Receiver::status() const {
  std::lock_guard<std::mutex> l(_m);
  RxStatus s = _st;
  s.running = _active;
  return s;
}

void Receiver::worker() {
  std::mt19937 rng(7);
  std::normal_distribution<float> nd(0, 1);
  const size_t N = Stream8822b::kBankSamples;
  std::vector<uint8_t> bank(N * 8);
  uint64_t seq = 0;
  double ph = 0;
  auto t_next = std::chrono::steady_clock::now();
  RxSettings s;
  BankMeta m;
  bool first = true;
  while (_active) {
    bool restart;
    {
      std::lock_guard<std::mutex> l(_m);
      restart = _restart || first;
      _restart = false;
      s = _set;
    }
    if (restart) {
      if (!first) std::this_thread::sleep_for(std::chrono::milliseconds(150));  // retune + stream restart
      first = false;
      m = {};
      m.gen = ++_gen;
      m.fs = rate_of(s.rate_sel);
      m.centre_hz = std::lround(s.freq / 5e6) * 5e6;
      m.shift_hz = s.freq - m.centre_hz;
      seq = 0;
      t_next = std::chrono::steady_clock::now();
      std::lock_guard<std::mutex> l(_m);
      _st.state = RxState::Streaming;
      _st.fs = m.fs;
      _st.centre_hz = m.centre_hz;
      _st.gen = m.gen;
    }
    // the recording at 40 MS/s, moved by (rec centre - hw centre); decimated naively for lower rates
    const int dec = static_cast<int>(std::lround(40e6 / m.fs));
    const double move = g_sim->rec_centre - m.centre_hz;
    const bool visible = std::fabs(move) < 30e6;
    const std::complex<float> rot(static_cast<float>(std::cos(2 * M_PI * move / 40e6)),
                                  static_cast<float>(std::sin(2 * M_PI * move / 40e6)));
    std::complex<float> lo(static_cast<float>(std::cos(ph)), static_cast<float>(std::sin(ph)));
    for (size_t i = 0; i < N; i++) {
      float re = 0, im = 0;
      for (int k = 0; k < dec; k++) {
        const size_t p = g_sim->pos;
        g_sim->pos = (g_sim->pos + 2) % g_sim->rec.size();
        if (visible) {
          const std::complex<float> x(g_sim->rec[p] / 16.f, g_sim->rec[p + 1] / 16.f);
          const auto y = x * lo;
          re += y.real();
          im += y.imag();
        }
        lo *= rot;
      }
      re = re / dec + 3 * nd(rng);  // a little noise so empty steps have a floor
      im = im / dec + 3 * nd(rng);
      const uint64_t li = static_cast<uint64_t>(static_cast<int>(std::lround(re)) & 0xFFF);
      const uint64_t lq = static_cast<uint64_t>(static_cast<int>(std::lround(im)) & 0xFFF);
      const uint64_t w = li | (lq << 12);
      std::memcpy(bank.data() + 8 * i, &w, 8);
    }
    ph = std::arg(lo);
    m.seq = seq++;
    _sink(bank.data(), bank.size(), m);
    {
      std::lock_guard<std::mutex> l(_m);
      _st.banks++;
    }
    t_next += std::chrono::nanoseconds(static_cast<int64_t>(N / m.fs * 1e9));
    std::this_thread::sleep_until(t_next);
  }
}

}  // namespace wsdr
