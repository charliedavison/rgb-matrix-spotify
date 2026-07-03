#include "now_playing_renderer.hpp"

#include "font5x7.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

namespace {

constexpr int kTextX = 2;
constexpr int kTextWidthMargin = 4;
constexpr double kMusicScrollSpeedPx = 18.0;
constexpr double kPodcastScrollSpeedPx = 10.0;
constexpr double kScrollStartPauseSec = 1.5;
constexpr double kScrollEndPauseSec = 1.0;
constexpr double kPodcastStartPauseSec = 2.0;
constexpr double kPodcastEndPauseSec = 1.5;

void set_pixel(ImageBuffer& frame, int size, int x, int y, util::Rgb color) {
  if (x < 0 || y < 0 || x >= size || y >= size) {
    return;
  }
  frame[y * size + x] = color;
}

void fill_rect(ImageBuffer& frame, int size, int x, int y, int w, int h, util::Rgb color) {
  for (int py = y; py < y + h; ++py) {
    for (int px = x; px < x + w; ++px) {
      set_pixel(frame, size, px, py, color);
    }
  }
}

const uint8_t* glyph_for_char(char ch) {
  if (ch < 32 || ch > 126) {
    ch = '?';
  }
  return kFont5x7[static_cast<unsigned char>(ch) - 32];
}

void draw_char(ImageBuffer& frame, int size, int x, int y, char ch, util::Rgb color) {
  const uint8_t* glyph = glyph_for_char(ch);
  for (int col = 0; col < 5; ++col) {
    uint8_t line = glyph[col];
    for (int row = 0; row < 7; ++row) {
      if (line & (1 << row)) {
        set_pixel(frame, size, x + col, y + row, color);
      }
    }
  }
}

void draw_string(ImageBuffer& frame, int size, int x, int y, const std::string& text, util::Rgb color) {
  int cursor = x;
  for (char ch : text) {
    draw_char(frame, size, cursor, y, ch, color);
    cursor += kFont5x7Width;
  }
}

void draw_string_right(ImageBuffer& frame, int size, int x_right, int y, const std::string& text, util::Rgb color) {
  const int width = static_cast<int>(text.size()) * kFont5x7Width;
  const int x = std::max(0, x_right - width);
  draw_string(frame, size, x, y, text, color);
}

std::string format_duration_ms(int64_t ms) {
  if (ms < 0) {
    ms = 0;
  }
  const int64_t total_seconds = ms / 1000;
  const int minutes = static_cast<int>(total_seconds / 60);
  const int seconds = static_cast<int>(total_seconds % 60);
  char buffer[16];
  std::snprintf(buffer, sizeof(buffer), "%d:%02d", minutes, seconds);
  return buffer;
}

struct ScrollLineState {
  std::string text;
  double offset = 0.0;
  double pause_remaining = 0.0;
  bool at_end_pause = false;
  double scroll_speed_px = kMusicScrollSpeedPx;
  double start_pause_sec = kScrollStartPauseSec;
  double end_pause_sec = kScrollEndPauseSec;

  void configure(bool is_podcast) {
    scroll_speed_px = is_podcast ? kPodcastScrollSpeedPx : kMusicScrollSpeedPx;
    start_pause_sec = is_podcast ? kPodcastStartPauseSec : kScrollStartPauseSec;
    end_pause_sec = is_podcast ? kPodcastEndPauseSec : kScrollEndPauseSec;
  }

  void sync(const std::string& new_text) {
    if (new_text == text) {
      return;
    }
    text = new_text;
    offset = 0.0;
    pause_remaining = start_pause_sec;
    at_end_pause = false;
  }

  int text_width_px() const {
    return static_cast<int>(text.size()) * kFont5x7Width;
  }

  void tick(double delta_seconds, int visible_width_px) {
    if (text_width_px() <= visible_width_px) {
      offset = 0.0;
      pause_remaining = 0.0;
      at_end_pause = false;
      return;
    }

    if (pause_remaining > 0.0) {
      pause_remaining = std::max(0.0, pause_remaining - delta_seconds);
      return;
    }

    const int gap = kFont5x7Width * 3;
    const double max_offset = text_width_px() + gap - visible_width_px;
    offset += scroll_speed_px * delta_seconds;

    if (offset >= max_offset) {
      offset = max_offset;
      if (!at_end_pause) {
        at_end_pause = true;
        pause_remaining = end_pause_sec;
      } else if (pause_remaining <= 0.0) {
        offset = 0.0;
        at_end_pause = false;
        pause_remaining = start_pause_sec;
      }
    }
  }

  void draw(ImageBuffer& frame, int size, int x, int y, int visible_width_px, util::Rgb color) const {
    if (text_width_px() <= visible_width_px) {
      draw_string(frame, size, x, y, text, color);
      return;
    }

    draw_string(frame, size, x - static_cast<int>(offset), y, text, color);
  }
};

struct NowPlayingScrollState {
  std::string track_key;
  bool is_podcast = false;
  ScrollLineState artist;
  ScrollLineState title;
};

ImageBuffer g_frame;
NowPlayingScrollState g_scroll;

void draw_podcast_badge(ImageBuffer& frame, int size) {
  const util::Rgb badge_bg{95, 36, 159};
  const util::Rgb badge_text{255, 255, 255};
  const int badge_w = 20;
  const int badge_h = 9;
  const int badge_x = size - badge_w - 1;
  const int badge_y = 1;
  fill_rect(frame, size, badge_x, badge_y, badge_w, badge_h, badge_bg);
  draw_string(frame, size, badge_x + 1, badge_y + 1, "POD", badge_text);
}

}  // namespace

const ImageBuffer& render_now_playing(const NowPlayingSnapshot& snapshot, int size, double delta_seconds) {
  if (g_frame.size() != static_cast<std::size_t>(size * size)) {
    g_frame.assign(static_cast<std::size_t>(size * size), util::Rgb{0, 0, 0});
  }

  std::fill(g_frame.begin(), g_frame.end(), util::Rgb{0, 0, 0});

  if (!snapshot.has_track) {
    g_scroll.track_key.clear();
    draw_string(g_frame, size, kTextX, (size - kFont5x7Height) / 2, "No track", util::Rgb{120, 120, 120});
    return g_frame;
  }

  const util::Rgb artist_color{180, 180, 180};
  const util::Rgb title_color{255, 255, 255};
  const util::Rgb time_color{140, 140, 140};
  const util::Rgb bar_bg{28, 28, 28};
  const util::Rgb bar_fill = snapshot.is_podcast ? util::Rgb{155, 84, 255} : util::Rgb{29, 185, 84};

  const std::string show_line = snapshot.artist.empty()
                                    ? (snapshot.is_podcast ? "Unknown show" : "Unknown artist")
                                    : snapshot.artist;
  const std::string title_line = snapshot.title.empty()
                                     ? (snapshot.is_podcast ? "Unknown episode" : "Unknown track")
                                     : snapshot.title;
  const std::string track_key = show_line + '\n' + title_line + (snapshot.is_podcast ? "\np" : "\nm");

  if (track_key != g_scroll.track_key) {
    g_scroll.track_key = track_key;
    g_scroll.is_podcast = snapshot.is_podcast;
    g_scroll.artist.configure(snapshot.is_podcast);
    g_scroll.title.configure(snapshot.is_podcast);
    g_scroll.artist.sync(show_line);
    g_scroll.title.sync(title_line);
  }

  const int visible_width_px = size - kTextWidthMargin;
  const double delta = std::max(0.0, delta_seconds);
  g_scroll.artist.tick(delta, visible_width_px);
  g_scroll.title.tick(delta, visible_width_px);

  g_scroll.artist.draw(g_frame, size, kTextX, 4, visible_width_px, artist_color);
  g_scroll.title.draw(g_frame, size, kTextX, 14, visible_width_px, title_color);

  if (snapshot.is_podcast) {
    draw_podcast_badge(g_frame, size);
  }

  const std::string elapsed = format_duration_ms(snapshot.progress_ms);
  const std::string total = format_duration_ms(snapshot.duration_ms);
  const int time_y = size - kFont5x7Height - 10;
  draw_string(g_frame, size, kTextX, time_y, elapsed, time_color);
  draw_string_right(g_frame, size, size - 2, time_y, total, time_color);

  const int bar_x = 2;
  const int bar_y = size - 7;
  const int bar_w = size - 4;
  const int bar_h = 5;
  fill_rect(g_frame, size, bar_x, bar_y, bar_w, bar_h, bar_bg);

  double ratio = 0.0;
  if (snapshot.duration_ms > 0) {
    ratio = static_cast<double>(snapshot.progress_ms) / static_cast<double>(snapshot.duration_ms);
    ratio = std::clamp(ratio, 0.0, 1.0);
  }

  const int fill_w = std::max(0, static_cast<int>(bar_w * ratio));
  if (fill_w > 0) {
    fill_rect(g_frame, size, bar_x, bar_y, fill_w, bar_h, bar_fill);
  }

  return g_frame;
}
