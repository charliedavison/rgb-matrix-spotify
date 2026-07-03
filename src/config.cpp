#include "config.hpp"

#include "util.hpp"

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string_view>
#include <system_error>

namespace {

void set_env_var(const std::string& key, const std::string& value, AppConfig& config) {
  if (key == "SPOTIFY_CLIENT_ID") {
    config.spotify_client_id = value;
  } else if (key == "SPOTIFY_CLIENT_SECRET") {
    config.spotify_client_secret = value;
  } else if (key == "SPOTIFY_REDIRECT_URI") {
    config.spotify_redirect_uri = value;
  }
}

void require_positive(double value, const char* name) {
  if (value <= 0.0) {
    throw std::runtime_error(std::string(name) + " must be greater than zero");
  }
}

}  // namespace

std::filesystem::path project_root(int argc, char** argv) {
  std::filesystem::path exe_dir;

#if defined(__linux__)
  std::error_code ec;
  const auto exe = std::filesystem::read_symlink("/proc/self/exe", ec);
  if (!ec) {
    exe_dir = exe.parent_path();
  }
#endif

  if (exe_dir.empty() && argc > 0 && argv[0] != nullptr) {
    std::error_code ec;
    exe_dir = std::filesystem::absolute(argv[0], ec).parent_path();
  }

  if (exe_dir.empty()) {
    return std::filesystem::current_path();
  }

  const auto dir_name = exe_dir.filename().string();
  if (dir_name == "bin" || dir_name == "build") {
    return exe_dir.parent_path();
  }

  return exe_dir;
}

void resolve_config_paths(AppConfig& config, const std::filesystem::path& root) {
  if (config.token_cache.is_relative()) {
    if (config.token_cache == std::filesystem::path(".cache/spotify_token.json")) {
      if (const char* home = std::getenv("HOME"); home && *home) {
        config.token_cache = std::filesystem::path(home) / ".cache/rgb-spotify/spotify_token.json";
        return;
      }
    }
    config.token_cache = root / config.token_cache;
  }
}

void load_env_file(const std::filesystem::path& path, AppConfig& config) {
  if (!std::filesystem::exists(path)) {
    return;
  }

  std::ifstream input(path);
  std::string line;
  while (std::getline(input, line)) {
    line = util::trim(line);
    if (line.empty() || line[0] == '#') {
      continue;
    }

    const auto eq = line.find('=');
    if (eq == std::string::npos) {
      continue;
    }

    const std::string key = util::trim(line.substr(0, eq));
    std::string value = util::trim(line.substr(eq + 1));
    if (value.size() >= 2 &&
        ((value.front() == '"' && value.back() == '"') ||
         (value.front() == '\'' && value.back() == '\''))) {
      value = value.substr(1, value.size() - 2);
    }
    set_env_var(key, value, config);
  }
}

AppConfig parse_args(int argc, char** argv) {
  AppConfig config;

  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    const auto next = [&]() -> std::string {
      if (i + 1 >= argc) {
        throw std::runtime_error("Missing value for " + arg);
      }
      return argv[++i];
    };

    if (arg == "--rows") {
      config.rows = std::stoi(next());
    } else if (arg == "--cols") {
      config.cols = std::stoi(next());
    } else if (arg == "--chain-length") {
      config.chain_length = std::stoi(next());
    } else if (arg == "--parallel") {
      config.parallel = std::stoi(next());
    } else if (arg == "--brightness") {
      config.brightness = std::stoi(next());
    } else if (arg == "--gpio-slowdown") {
      config.gpio_slowdown = std::stoi(next());
    } else if (arg == "--hardware-mapping") {
      config.hardware_mapping = next();
    } else if (arg == "--pwm-bits") {
      config.pwm_bits = std::stoi(next());
    } else if (arg == "--limit-refresh-rate-hz") {
      config.limit_refresh_rate_hz = std::stoi(next());
    } else if (arg == "--no-hardware-pulse") {
      config.no_hardware_pulse = true;
    } else if (arg == "--poll-seconds") {
      config.poll_seconds = std::stod(next());
      require_positive(config.poll_seconds, "--poll-seconds");
    } else if (arg == "--fps") {
      config.fps = std::stod(next());
      require_positive(config.fps, "--fps");
    } else if (arg == "--rpm") {
      config.rpm = std::stod(next());
      require_positive(config.rpm, "--rpm");
    } else if (arg == "--token-cache") {
      config.token_cache = next();
    } else if (arg == "--mock-output") {
      config.mock_output = next();
    } else if (arg == "--preview-frames") {
      config.preview_frames = next();
    } else if (arg == "--auth-only") {
      config.auth_only = true;
    } else if (arg == "--test-pattern") {
      config.test_pattern = true;
    } else if (arg == "--once") {
      config.once = true;
    } else if (arg == "--no-browser") {
      config.no_browser = true;
    } else if (arg == "--help" || arg == "-h") {
      std::cout
          << "Usage: spotify-matrix [options]\n\n"
          << "Shows Spotify album art as a spinning vinyl record on a 64x64 RGB matrix.\n\n"
          << "Matrix options:\n"
          << "  --rows N                 Panel rows (default 64)\n"
          << "  --cols N                 Panel cols (default 64)\n"
          << "  --chain-length N         Panel chain length (default 1)\n"
          << "  --parallel N             Parallel chains (default 1)\n"
          << "  --brightness N           Brightness 1-100 (default 65)\n"
          << "  --gpio-slowdown N        GPIO slowdown (default 2, use 4 on Pi Zero)\n"
          << "  --hardware-mapping NAME  regular or adafruit-hat (default regular)\n"
          << "  --pwm-bits N             PWM bits (default 11)\n"
          << "  --limit-refresh-rate-hz N  Refresh cap (default 120)\n"
          << "  --no-hardware-pulse      Disable hardware pulsing\n\n"
          << "Runtime options:\n"
          << "  --poll-seconds N         Spotify poll interval (default 2)\n"
          << "  --fps N                  Frame rate (default 20)\n"
          << "  --rpm N                  Spin speed when playing (default 20)\n"
          << "  --token-cache PATH       OAuth token cache (default .cache/spotify_token.json)\n"
          << "  --mock-output PATH       Write PNG frame instead of matrix hardware\n"
          << "  --preview-frames DIR     Render sample disk frames and exit\n"
          << "  --auth-only              Authorize Spotify and exit\n"
          << "  --test-pattern           Show moving color bars\n"
          << "  --once                   Render one frame and exit\n"
          << "  --no-browser             Print auth URL without opening browser\n";
      std::exit(0);
    } else {
      throw std::runtime_error("Unknown argument: " + arg);
    }
  }

  load_env_file(project_root(argc, argv) / ".env", config);

  if (const char* value = std::getenv("SPOTIFY_CLIENT_ID")) {
    config.spotify_client_id = value;
  }
  if (const char* value = std::getenv("SPOTIFY_CLIENT_SECRET")) {
    config.spotify_client_secret = value;
  }
  if (const char* value = std::getenv("SPOTIFY_REDIRECT_URI")) {
    config.spotify_redirect_uri = value;
  }

  return config;
}
