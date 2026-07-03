#include "schedule.hpp"

#include <algorithm>
#include <ctime>
#include <stdexcept>

namespace {

int local_minutes_since_midnight() {
  const std::time_t now = std::time(nullptr);
  std::tm local_time{};
#if defined(_WIN32)
  localtime_s(&local_time, &now);
#else
  localtime_r(&now, &local_time);
#endif
  return local_time.tm_hour * 60 + local_time.tm_min;
}

bool in_minute_window(int start_minutes, int end_minutes, int now_minutes) {
  if (start_minutes == end_minutes) {
    return false;
  }
  if (start_minutes < end_minutes) {
    return now_minutes >= start_minutes && now_minutes < end_minutes;
  }
  return now_minutes >= start_minutes || now_minutes < end_minutes;
}

}  // namespace

bool parse_clock_time(const std::string& value, int& minutes_out) {
  const auto colon = value.find(':');
  if (colon == std::string::npos) {
    return false;
  }

  try {
    const int hours = std::stoi(value.substr(0, colon));
    const int minutes = std::stoi(value.substr(colon + 1));
    if (hours < 0 || hours > 23 || minutes < 0 || minutes > 59) {
      return false;
    }
    minutes_out = hours * 60 + minutes;
    return true;
  } catch (const std::exception&) {
    return false;
  }
}

bool is_night_time(const ScheduleConfig& config) {
  if (!config.night_enabled) {
    return false;
  }
  return in_minute_window(config.night_start_minutes, config.night_end_minutes, local_minutes_since_midnight());
}

int effective_brightness(const ScheduleConfig& config, int normal_brightness) {
  if (!config.night_enabled || !is_night_time(config)) {
    return normal_brightness;
  }
  if (config.night_brightness <= 0) {
    return 0;
  }
  return std::min(config.night_brightness, normal_brightness);
}

bool should_idle_off(
    const ScheduleConfig& config,
    bool has_track,
    std::chrono::steady_clock::time_point idle_since,
    std::chrono::steady_clock::time_point now) {
  if (config.idle_off_minutes <= 0 || has_track) {
    return false;
  }

  const auto idle_for = std::chrono::duration_cast<std::chrono::minutes>(now - idle_since);
  return idle_for.count() >= config.idle_off_minutes;
}
