#pragma once

#include "http_client.hpp"

#include "json.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

struct PlaybackInfo {
  std::string key;
  std::string image_url;
  std::string title;
  std::string artist;
  int64_t progress_ms = 0;
  int64_t duration_ms = 0;
  bool is_playing = false;
  bool is_podcast = false;
};

class SpotifyClient {
 public:
  SpotifyClient(
      std::string client_id,
      std::string client_secret,
      std::string redirect_uri,
      std::filesystem::path token_cache,
      bool open_browser);

  void authorize();
  std::optional<PlaybackInfo> get_currently_playing(int auth_retry = 0);

 private:
  std::string valid_access_token();
  void load_token();
  void ensure_token_cache_directory() const;
  void save_token(const nlohmann::json& token);
  nlohmann::json authorize_interactive();
  HttpResponse post_token_request(const std::map<std::string, std::string>& data);
  nlohmann::json post_token(const std::map<std::string, std::string>& data);
  void refresh_access_token();
  bool is_invalid_grant(const HttpResponse& response) const;
  void clear_token_cache();
  std::optional<PlaybackInfo> playback_from_json(const nlohmann::json& playback) const;
  void raise_http_error(const HttpResponse& response, const std::string& context) const;

  std::string client_id_;
  std::string client_secret_;
  std::string redirect_uri_;
  std::filesystem::path token_cache_;
  bool open_browser_;
  HttpClient http_;
  nlohmann::json token_;
};
