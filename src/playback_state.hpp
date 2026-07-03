#pragma once

#include "image_renderer.hpp"

#include <chrono>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>

struct SharedPlaybackState {
  std::mutex mutex;
  std::optional<std::string> art_key;
  std::optional<std::string> image_url;
  std::optional<LoadedImage> image;
  std::string title;
  std::string artist;
  int64_t progress_ms = 0;
  int64_t duration_ms = 0;
  bool is_playing = false;
  bool is_podcast = false;
  std::chrono::steady_clock::time_point progress_updated_at = std::chrono::steady_clock::now();
};

int64_t interpolated_progress_ms(const SharedPlaybackState& state);

struct NowPlayingSnapshot {
  std::string title;
  std::string artist;
  int64_t progress_ms = 0;
  int64_t duration_ms = 0;
  bool has_track = false;
  bool is_podcast = false;
};

NowPlayingSnapshot snapshot_now_playing(const SharedPlaybackState& state);
