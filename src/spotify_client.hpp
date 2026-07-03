#pragma once

#include "http_client.hpp"

#include "json.hpp"

#include <filesystem>
#include <memory>
#include <optional>
#include <string>

struct PlaybackArt {
  std::string key;
  std::string image_url;
  bool is_playing = false;
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
  std::optional<PlaybackArt> get_currently_playing();

 private:
  std::string valid_access_token();
  void load_token();
  void save_token(const nlohmann::json& token);
  nlohmann::json authorize_interactive();
  nlohmann::json post_token(const std::map<std::string, std::string>& data);
  void refresh_access_token();
  std::optional<PlaybackArt> playback_art_from_json(const nlohmann::json& playback) const;
  void raise_http_error(const HttpResponse& response, const std::string& context) const;

  std::string client_id_;
  std::string client_secret_;
  std::string redirect_uri_;
  std::filesystem::path token_cache_;
  bool open_browser_;
  HttpClient http_;
  nlohmann::json token_;
};
