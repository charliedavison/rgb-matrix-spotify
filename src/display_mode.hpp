#pragma once

#include <atomic>
#include <string>

enum class DisplayMode {
  kVinyl,
  kNowPlaying,
  kVisualizer,
  kOff,
};

DisplayMode parse_display_mode(const std::string& value);
const char* display_mode_name(DisplayMode mode);
const char* display_mode_label(DisplayMode mode);

class DisplayModeStore {
 public:
  DisplayMode get() const { return static_cast<DisplayMode>(mode_.load()); }
  void set(DisplayMode mode) { mode_.store(static_cast<int>(mode)); }

 private:
  std::atomic<int> mode_{static_cast<int>(DisplayMode::kVinyl)};
};
