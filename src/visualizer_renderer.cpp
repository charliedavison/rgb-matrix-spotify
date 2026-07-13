#include "visualizer_renderer.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace {

constexpr int kSpokeCount = 16;
constexpr double kDefaultTempo = 120.0;
constexpr double kTwoPi = 6.283185307179586;

ImageBuffer g_frame;
std::string g_track_key;
int g_last_beat_index = -1;
double g_pulse = 0.0;
double g_hue = 0.0;
std::vector<double> g_spoke_energy(kSpokeCount, 0.0);

void set_pixel(ImageBuffer& frame, int size, int x, int y, util::Rgb color) {
  if (x < 0 || y < 0 || x >= size || y >= size) {
    return;
  }
  frame[y * size + x] = color;
}

void blend_pixel(ImageBuffer& frame, int size, int x, int y, util::Rgb color, double alpha) {
  if (x < 0 || y < 0 || x >= size || y >= size || alpha <= 0.0) {
    return;
  }
  alpha = std::clamp(alpha, 0.0, 1.0);
  util::Rgb& dest = frame[y * size + x];
  dest.r = static_cast<uint8_t>(dest.r * (1.0 - alpha) + color.r * alpha);
  dest.g = static_cast<uint8_t>(dest.g * (1.0 - alpha) + color.g * alpha);
  dest.b = static_cast<uint8_t>(dest.b * (1.0 - alpha) + color.b * alpha);
}

util::Rgb hsv_to_rgb(double h, double s, double v) {
  h = std::fmod(h, 360.0);
  if (h < 0.0) {
    h += 360.0;
  }
  s = std::clamp(s, 0.0, 1.0);
  v = std::clamp(v, 0.0, 1.0);

  const double c = v * s;
  const double x = c * (1.0 - std::fabs(std::fmod(h / 60.0, 2.0) - 1.0));
  const double m = v - c;
  double r = 0.0;
  double g = 0.0;
  double b = 0.0;
  if (h < 60.0) {
    r = c;
    g = x;
  } else if (h < 120.0) {
    r = x;
    g = c;
  } else if (h < 180.0) {
    g = c;
    b = x;
  } else if (h < 240.0) {
    g = x;
    b = c;
  } else if (h < 300.0) {
    r = x;
    b = c;
  } else {
    r = c;
    b = x;
  }
  return util::Rgb{
      static_cast<uint8_t>((r + m) * 255.0),
      static_cast<uint8_t>((g + m) * 255.0),
      static_cast<uint8_t>((b + m) * 255.0),
  };
}

struct BeatCursor {
  int index = 0;
  double phase = 0.0;       // 0..1 within current beat
  bool is_downbeat = false;
  double intensity = 0.0;   // decays from 1 on beat onset
};

BeatCursor locate_beat(const AudioAnalysis& analysis, double position_sec) {
  BeatCursor cursor;
  if (!analysis.valid || analysis.beat_starts.empty()) {
    const double tempo = analysis.tempo > 1.0 ? analysis.tempo : kDefaultTempo;
    const double beats = position_sec * tempo / 60.0;
    cursor.index = std::max(0, static_cast<int>(std::floor(beats)));
    cursor.phase = beats - cursor.index;
    cursor.is_downbeat = (cursor.index % 4) == 0;
    return cursor;
  }

  const auto& starts = analysis.beat_starts;
  auto it = std::upper_bound(starts.begin(), starts.end(), position_sec);
  int index = static_cast<int>(std::distance(starts.begin(), it)) - 1;
  if (index < 0) {
    index = 0;
  }
  if (index >= static_cast<int>(starts.size())) {
    index = static_cast<int>(starts.size()) - 1;
  }

  const double start = starts[static_cast<std::size_t>(index)];
  const double next =
      (index + 1 < static_cast<int>(starts.size())) ? starts[static_cast<std::size_t>(index + 1)]
                                                    : start + (60.0 / std::max(1.0, analysis.tempo));
  const double duration = std::max(0.001, next - start);

  cursor.index = index;
  cursor.phase = std::clamp((position_sec - start) / duration, 0.0, 1.0);
  cursor.is_downbeat = (index % 4) == 0;
  return cursor;
}

void draw_radial_spokes(
    ImageBuffer& frame,
    int size,
    const std::vector<double>& energy,
    util::Rgb color,
    util::Rgb accent) {
  const double cx = (size - 1) * 0.5;
  const double cy = (size - 1) * 0.5;
  const double max_radius = size * 0.46;

  for (int i = 0; i < kSpokeCount; ++i) {
    const double angle = (static_cast<double>(i) / kSpokeCount) * kTwoPi - kTwoPi * 0.25;
    const double level = std::clamp(energy[static_cast<std::size_t>(i)], 0.0, 1.0);
    const double inner = max_radius * 0.18;
    const double outer = inner + (max_radius - inner) * (0.25 + 0.75 * level);
    const util::Rgb spoke_color = (i % 4 == 0) ? accent : color;

    const int steps = static_cast<int>(outer - inner) + 2;
    for (int s = 0; s <= steps; ++s) {
      const double t = static_cast<double>(s) / std::max(1, steps);
      const double radius = inner + (outer - inner) * t;
      const double fade = 0.35 + 0.65 * level * (1.0 - t * 0.35);
      const int x = static_cast<int>(std::lround(cx + std::cos(angle) * radius));
      const int y = static_cast<int>(std::lround(cy + std::sin(angle) * radius));
      blend_pixel(frame, size, x, y, spoke_color, fade);
      // Thicken spokes slightly for visibility on a 64×64 panel.
      blend_pixel(frame, size, x + 1, y, spoke_color, fade * 0.7);
      blend_pixel(frame, size, x, y + 1, spoke_color, fade * 0.7);
    }
  }
}

void draw_pulse_ring(ImageBuffer& frame, int size, double phase, double strength, util::Rgb color) {
  if (strength <= 0.01) {
    return;
  }
  const double cx = (size - 1) * 0.5;
  const double cy = (size - 1) * 0.5;
  const double max_radius = size * 0.48;
  const double radius = max_radius * (0.12 + 0.88 * phase);
  const double alpha = strength * (1.0 - phase) * (1.0 - phase);

  const int samples = std::max(48, size * 3);
  for (int i = 0; i < samples; ++i) {
    const double angle = (static_cast<double>(i) / samples) * kTwoPi;
    const int x = static_cast<int>(std::lround(cx + std::cos(angle) * radius));
    const int y = static_cast<int>(std::lround(cy + std::sin(angle) * radius));
    blend_pixel(frame, size, x, y, color, alpha);
  }
}

void draw_center_orb(ImageBuffer& frame, int size, double pulse, util::Rgb color) {
  const int cx = size / 2;
  const int cy = size / 2;
  const int radius = std::max(2, static_cast<int>(std::lround(2.0 + pulse * 5.0)));
  for (int y = cy - radius; y <= cy + radius; ++y) {
    for (int x = cx - radius; x <= cx + radius; ++x) {
      const int dx = x - cx;
      const int dy = y - cy;
      const double dist = std::sqrt(static_cast<double>(dx * dx + dy * dy));
      if (dist > radius) {
        continue;
      }
      const double alpha = (1.0 - dist / (radius + 0.01)) * (0.45 + 0.55 * pulse);
      blend_pixel(frame, size, x, y, color, alpha);
    }
  }
}

void draw_beat_blocks(ImageBuffer& frame, int size, int beat_in_bar, double pulse, util::Rgb color) {
  // Four corner blocks light in sequence around the bar (1-2-3-4).
  const int block = std::max(3, size / 10);
  const int inset = 1;
  const struct {
    int x;
    int y;
  } corners[4] = {
      {inset, inset},
      {size - inset - block, inset},
      {size - inset - block, size - inset - block},
      {inset, size - inset - block},
  };

  for (int i = 0; i < 4; ++i) {
    const bool active = i == beat_in_bar;
    const double alpha = active ? (0.35 + 0.65 * pulse) : 0.12;
    for (int y = 0; y < block; ++y) {
      for (int x = 0; x < block; ++x) {
        blend_pixel(frame, size, corners[i].x + x, corners[i].y + y, color, alpha);
      }
    }
  }
}

}  // namespace

const ImageBuffer& render_visualizer(const NowPlayingSnapshot& snapshot, int size, double delta_seconds) {
  if (static_cast<int>(g_frame.size()) != size * size) {
    g_frame.assign(static_cast<std::size_t>(size * size), util::Rgb{0, 0, 0});
  }

  const std::string track_key = snapshot.title + "|" + snapshot.artist + "|" +
                                std::to_string(snapshot.duration_ms);
  if (track_key != g_track_key) {
    g_track_key = track_key;
    g_last_beat_index = -1;
    g_pulse = 0.0;
    std::fill(g_spoke_energy.begin(), g_spoke_energy.end(), 0.0);
  }

  // Slow hue drift across the track; keeps colours moving even between beats.
  if (snapshot.duration_ms > 0) {
    g_hue = 200.0 + 140.0 * (static_cast<double>(snapshot.progress_ms) / snapshot.duration_ms);
  } else if (snapshot.is_playing) {
    g_hue = std::fmod(g_hue + delta_seconds * 12.0, 360.0);
  }

  const double position_sec = snapshot.progress_ms / 1000.0;
  AudioAnalysis analysis = snapshot.analysis;
  if (!analysis.valid) {
    analysis = synthesize_beats(snapshot.duration_ms / 1000.0, kDefaultTempo);
  }

  BeatCursor beat = locate_beat(analysis, position_sec);

  if (beat.index != g_last_beat_index) {
    g_last_beat_index = beat.index;
    g_pulse = beat.is_downbeat ? 1.0 : 0.72;

    // Energize a rotating subset of spokes on each beat.
    const int focus = beat.index % kSpokeCount;
    for (int i = 0; i < kSpokeCount; ++i) {
      const int dist = std::min(std::abs(i - focus), kSpokeCount - std::abs(i - focus));
      const double hit = (dist == 0) ? 1.0 : (dist == 1 ? 0.55 : (dist == 2 ? 0.22 : 0.0));
      g_spoke_energy[static_cast<std::size_t>(i)] =
          std::max(g_spoke_energy[static_cast<std::size_t>(i)], hit * g_pulse);
    }
  }

  if (snapshot.is_playing) {
    g_pulse = std::max(0.0, g_pulse - delta_seconds * 3.2);
    for (double& energy : g_spoke_energy) {
      energy = std::max(0.0, energy - delta_seconds * 1.8);
      // Soft residual bounce within the beat so spokes never go fully flat while playing.
      energy = std::max(energy, 0.08 + 0.12 * (1.0 - beat.phase));
    }
  }

  const util::Rgb bg{4, 4, 10};
  std::fill(g_frame.begin(), g_frame.end(), bg);

  // Soft vignette so the panel doesn't look flat.
  const double cx = (size - 1) * 0.5;
  const double cy = (size - 1) * 0.5;
  for (int y = 0; y < size; ++y) {
    for (int x = 0; x < size; ++x) {
      const double dx = (x - cx) / (size * 0.55);
      const double dy = (y - cy) / (size * 0.55);
      const double falloff = std::clamp(1.0 - (dx * dx + dy * dy), 0.0, 1.0);
      blend_pixel(g_frame, size, x, y, hsv_to_rgb(g_hue, 0.55, 0.18), 0.35 * falloff);
    }
  }

  const util::Rgb primary = hsv_to_rgb(g_hue, 0.85, 0.95);
  const util::Rgb accent = hsv_to_rgb(g_hue + 40.0, 0.9, 1.0);
  const util::Rgb pulse_color = hsv_to_rgb(g_hue - 25.0, 0.7, 1.0);

  draw_radial_spokes(g_frame, size, g_spoke_energy, primary, accent);
  draw_pulse_ring(g_frame, size, beat.phase, g_pulse, pulse_color);
  draw_center_orb(g_frame, size, g_pulse, accent);
  draw_beat_blocks(g_frame, size, beat.index % 4, g_pulse, accent);

  return g_frame;
}
