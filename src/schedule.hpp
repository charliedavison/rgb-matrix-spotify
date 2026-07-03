#pragma once

#include <chrono>
#include <string>

struct ScheduleConfig {
  bool night_enabled = false;
  int night_start_minutes = 23 * 60;
  int night_end_minutes = 7 * 60;
  int night_brightness = 0;
  int idle_off_minutes = 0;
};

bool parse_clock_time(const std::string& value, int& minutes_out);

bool is_night_time(const ScheduleConfig& config);

int effective_brightness(const ScheduleConfig& config, int normal_brightness);

bool should_idle_off(
    const ScheduleConfig& config,
    bool has_track,
    std::chrono::steady_clock::time_point idle_since,
    std::chrono::steady_clock::time_point now);
