#include "display_mode.hpp"

#include <stdexcept>

DisplayMode parse_display_mode(const std::string& value) {
  if (value == "vinyl" || value == "record") {
    return DisplayMode::kVinyl;
  }
  if (value == "nowplaying" || value == "now-playing" || value == "text") {
    return DisplayMode::kNowPlaying;
  }
  throw std::runtime_error("Unknown display mode: " + value);
}

const char* display_mode_name(DisplayMode mode) {
  switch (mode) {
    case DisplayMode::kVinyl:
      return "vinyl";
    case DisplayMode::kNowPlaying:
      return "nowplaying";
  }
  return "vinyl";
}

const char* display_mode_label(DisplayMode mode) {
  switch (mode) {
    case DisplayMode::kVinyl:
      return "Spinning vinyl";
    case DisplayMode::kNowPlaying:
      return "Track info";
  }
  return "Spinning vinyl";
}
