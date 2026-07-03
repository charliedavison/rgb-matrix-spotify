#include "playback_state.hpp"

namespace {

int64_t clamp_ms(int64_t value, int64_t min_value, int64_t max_value) {
  if (value < min_value) {
    return min_value;
  }
  if (value > max_value) {
    return max_value;
  }
  return value;
}

}  // namespace

int64_t interpolated_progress_ms(const SharedPlaybackState& state) {
  if (state.duration_ms <= 0) {
    return state.progress_ms;
  }
  if (!state.is_playing) {
    return clamp_ms(state.progress_ms, 0, state.duration_ms);
  }

  const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now() - state.progress_updated_at);
  return clamp_ms(state.progress_ms + elapsed.count(), 0, state.duration_ms);
}

NowPlayingSnapshot snapshot_now_playing(const SharedPlaybackState& state) {
  NowPlayingSnapshot snapshot;
  snapshot.has_track = state.art_key.has_value();
  snapshot.title = state.title;
  snapshot.artist = state.artist;
  snapshot.duration_ms = state.duration_ms;
  snapshot.progress_ms = interpolated_progress_ms(state);
  return snapshot;
}
