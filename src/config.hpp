#pragma once

#include <filesystem>
#include <string>

struct AppConfig {
  int rows = 64;
  int cols = 64;
  int chain_length = 1;
  int parallel = 1;
  int brightness = 65;
  int gpio_slowdown = 2;
  std::string hardware_mapping = "regular";
  int pwm_bits = 8;
  int pwm_lsb_nanoseconds = 130;
  int pwm_dither_bits = 0;
  int scan_mode = 1;
  int limit_refresh_rate_hz = 90;
  bool no_hardware_pulse = false;
  bool no_busy_waiting = true;

  double poll_seconds = 3.0;
  double fps = 15.0;
  double rpm = 20.0;

  std::filesystem::path token_cache = ".cache/spotify_token.json";
  std::filesystem::path mock_output;
  std::filesystem::path preview_frames;
  bool auth_only = false;
  bool test_pattern = false;
  bool once = false;
  bool no_browser = false;

  std::string web_host = "0.0.0.0";
  int web_port = 8080;
  bool no_web_ui = false;

  std::string spotify_client_id;
  std::string spotify_client_secret;
  std::string spotify_redirect_uri = "http://127.0.0.1:8888/callback";
};

AppConfig parse_args(int argc, char** argv);
void load_env_file(const std::filesystem::path& path, AppConfig& config);
std::filesystem::path project_root(int argc, char** argv);
void resolve_config_paths(AppConfig& config, const std::filesystem::path& root);
