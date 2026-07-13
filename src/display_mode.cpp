#include "display_mode.hpp"

#include <stdexcept>

DisplayMode parse_display_mode(const std::string& value) {
  if (value == "vinyl" || value == "record") {
    return DisplayMode::kVinyl;
  }
  if (value == "nowplaying" || value == "now-playing" || value == "text") {
    return DisplayMode::kNowPlaying;
  }
  if (value == "visualizer" || value == "visualiser" || value == "viz" || value == "beats") {
    return DisplayMode::kVisualizer;
  }
  if (value == "off" || value == "blank" || value == "none") {
    return DisplayMode::kOff;
  }
  throw std::runtime_error("Unknown display mode: " + value);
}

const char* display_mode_name(DisplayMode mode) {
  switch (mode) {
    case DisplayMode::kVinyl:
      return "vinyl";
    case DisplayMode::kNowPlaying:
      return "nowplaying";
    case DisplayMode::kVisualizer:
      return "visualizer";
    case DisplayMode::kOff:
      return "off";
  }
  return "vinyl";
}

const char* display_mode_label(DisplayMode mode) {
  switch (mode) {
    case DisplayMode::kVinyl:
      return "Spinning vinyl";
    case DisplayMode::kNowPlaying:
      return "Track info";
    case DisplayMode::kVisualizer:
      return "Beat visualiser";
    case DisplayMode::kOff:
      return "Off";
  }
  return "Spinning vinyl";
}
