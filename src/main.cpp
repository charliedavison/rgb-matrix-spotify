#include "config.hpp"
#include "display.hpp"
#include "http_client.hpp"
#include "image_renderer.hpp"
#include "spotify_client.hpp"

#include <atomic>
#include <chrono>
#include <cmath>
#include <csignal>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

namespace {

std::atomic<bool> g_stop{false};

void handle_signal(int) {
  g_stop = true;
}

struct SharedPlaybackState {
  std::mutex mutex;
  std::optional<std::string> art_key;
  std::optional<std::string> image_url;
  std::optional<LoadedImage> image;
  bool is_playing = false;
};

void validate_config(const AppConfig& config) {
  if (config.preview_frames.empty() && config.test_pattern) {
    return;
  }
  if (config.preview_frames.empty() && !config.test_pattern) {
    if (config.spotify_client_id.empty() || config.spotify_client_secret.empty() ||
        config.spotify_redirect_uri.empty()) {
      throw std::runtime_error(
          "Missing required environment values: SPOTIFY_CLIENT_ID, SPOTIFY_CLIENT_SECRET, SPOTIFY_REDIRECT_URI");
    }
  }
}

void poll_spotify(SpotifyClient& spotify, HttpClient& http, SharedPlaybackState& state, double poll_seconds) {
  std::optional<std::string> last_status;

  while (!g_stop.load()) {
    try {
      const auto art = spotify.get_currently_playing();
      if (art) {
        bool needs_download = false;
        std::string download_url;
        {
          std::lock_guard lock(state.mutex);
          needs_download =
              !state.art_key || !state.image_url || *state.art_key != art->key || *state.image_url != art->image_url;
          if (needs_download) {
            download_url = art->image_url;
          }
        }

        std::optional<LoadedImage> downloaded;
        if (needs_download) {
          downloaded = download_image(http, download_url);
        }

        {
          std::lock_guard lock(state.mutex);
          state.art_key = art->key;
          state.image_url = art->image_url;
          state.is_playing = art->is_playing;
          if (downloaded) {
            state.image = std::move(*downloaded);
          }
        }

        const std::string status = "art found, is_playing=" + std::string(art->is_playing ? "true" : "false");
        if (!last_status || *last_status != status) {
          std::cout << "Spotify: " << status << std::endl;
          last_status = status;
        }
      } else {
        std::lock_guard lock(state.mutex);
        state.art_key.reset();
        state.image_url.reset();
        state.image.reset();
        state.is_playing = false;

        const std::string status = "no currently playing item";
        if (!last_status || *last_status != status) {
          std::cout << "Spotify: " << status << std::endl;
          last_status = status;
        }
      }
    } catch (const std::exception& ex) {
      std::cerr << "Spotify poll failed: " << ex.what() << std::endl;
    }

    for (int i = 0; i < static_cast<int>(poll_seconds * 10) && !g_stop.load(); ++i) {
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
  }
}

}  // namespace

int main(int argc, char** argv) {
  try {
    AppConfig config = parse_args(argc, argv);
    const auto root = project_root(argc, argv);
    resolve_config_paths(config, root);

    if (!config.preview_frames.empty()) {
      render_preview_frames(config.preview_frames);
      return 0;
    }

    validate_config(config);

    SpotifyClient spotify(
        config.spotify_client_id,
        config.spotify_client_secret,
        config.spotify_redirect_uri,
        config.token_cache,
        !config.no_browser);

    if (config.auth_only) {
      spotify.authorize();
      std::cout << "Spotify token cached at " << config.token_cache << std::endl;
      return 0;
    }

    auto display = create_display(config);
    const int size = std::min(config.rows, config.cols);

    if (config.test_pattern) {
      std::signal(SIGINT, handle_signal);
      int offset = 0;
      while (!g_stop.load()) {
        display->show(render_test_pattern(size, offset), size, size);
        offset = (offset + 1) % size;
        std::this_thread::sleep_for(std::chrono::milliseconds(static_cast<int>(1000.0 / config.fps)));
      }
      display->clear();
      return 0;
    }

    const ImageBuffer idle = render_idle(size);
    SharedPlaybackState playback_state;
    HttpClient http;
    RecordRenderer record_renderer;
    std::optional<std::string> prepared_art_key;

    std::signal(SIGINT, handle_signal);

    std::thread poll_thread([&]() {
      poll_spotify(spotify, http, playback_state, config.poll_seconds);
    });

    double angle = 0.0;
    using clock = std::chrono::steady_clock;
    auto last_frame = clock::now();

    while (!g_stop.load()) {
      const auto frame_start = clock::now();

      const LoadedImage* current_art = nullptr;
      bool is_playing = false;
      std::optional<std::string> art_key;
      {
        std::lock_guard lock(playback_state.mutex);
        art_key = playback_state.art_key;
        if (playback_state.image) {
          current_art = &*playback_state.image;
        }
        is_playing = playback_state.is_playing;
      }

      if (art_key != prepared_art_key) {
        record_renderer.prepare(current_art, size);
        prepared_art_key = art_key;
      }

      const auto now = clock::now();
      const double delta = std::chrono::duration<double>(now - last_frame).count();
      last_frame = now;

      if (is_playing && current_art) {
        angle = std::fmod(angle + 360.0 * (config.rpm / 60.0) * delta, 360.0);
      }

      const ImageBuffer& frame =
          current_art ? record_renderer.render(angle) : idle;
      display->show(frame, size, size);

      if (config.once) {
        break;
      }

      const double frame_seconds = 1.0 / config.fps;
      const double elapsed = std::chrono::duration<double>(clock::now() - frame_start).count();
      const double sleep_for = std::max(0.0, frame_seconds - elapsed);
      if (sleep_for > 0.0) {
        std::this_thread::sleep_for(std::chrono::duration<double>(sleep_for));
      }
    }

    g_stop = true;
    if (poll_thread.joinable()) {
      poll_thread.join();
    }
    display->clear();
    return 0;
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }
}
