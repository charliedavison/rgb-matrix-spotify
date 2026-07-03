#include "spotify_client.hpp"

#include "oauth_server.hpp"
#include "util.hpp"

#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <thread>

namespace {

constexpr const char* kAuthUrl = "https://accounts.spotify.com/authorize";
constexpr const char* kTokenUrl = "https://accounts.spotify.com/api/token";
constexpr const char* kCurrentlyPlayingUrl = "https://api.spotify.com/v1/me/player/currently-playing";
constexpr const char* kScope = "user-read-currently-playing";

double now_seconds() {
  using clock = std::chrono::system_clock;
  return std::chrono::duration<double>(clock::now().time_since_epoch()).count();
}

struct RedirectParts {
  std::string host;
  int port = 80;
  std::string path;
};

RedirectParts parse_redirect_uri(const std::string& redirect_uri) {
  const auto scheme_end = redirect_uri.find("://");
  if (scheme_end == std::string::npos) {
    throw std::runtime_error("Invalid redirect URI: " + redirect_uri);
  }

  const auto host_start = scheme_end + 3;
  const auto path_start = redirect_uri.find('/', host_start);
  const std::string host_port =
      path_start == std::string::npos ? redirect_uri.substr(host_start)
                                    : redirect_uri.substr(host_start, path_start - host_start);

  RedirectParts parts;
  parts.path = path_start == std::string::npos ? "/callback" : redirect_uri.substr(path_start);

  const auto colon = host_port.find(':');
  if (colon == std::string::npos) {
    parts.host = host_port;
    parts.port = 80;
  } else {
    parts.host = host_port.substr(0, colon);
    parts.port = std::stoi(host_port.substr(colon + 1));
  }

  if (parts.host != "127.0.0.1" && parts.host != "localhost") {
    throw std::runtime_error("This program expects a localhost Spotify redirect URI.");
  }
  return parts;
}

void open_browser(const std::string& url) {
#if defined(__linux__)
  if (std::system(("xdg-open \"" + url + "\" >/dev/null 2>&1").c_str()) == 0) {
    return;
  }
#endif
  (void)url;
}

}  // namespace

SpotifyClient::SpotifyClient(
    std::string client_id,
    std::string client_secret,
    std::string redirect_uri,
    std::filesystem::path token_cache,
    bool open_browser)
    : client_id_(std::move(client_id)),
      client_secret_(std::move(client_secret)),
      redirect_uri_(std::move(redirect_uri)),
      token_cache_(std::move(token_cache)),
      open_browser_(open_browser) {
  load_token();
}

void SpotifyClient::authorize() {
  (void)valid_access_token();
}

void SpotifyClient::load_token() {
  if (!std::filesystem::exists(token_cache_)) {
    token_ = nlohmann::json::object();
    return;
  }

  std::ifstream input(token_cache_);
  if (!input) {
    token_ = nlohmann::json::object();
    return;
  }

  try {
    input >> token_;
  } catch (const std::exception&) {
    token_ = nlohmann::json::object();
    return;
  }

  if (!token_.is_object() || !token_.contains("access_token")) {
    token_ = nlohmann::json::object();
  }
}

void SpotifyClient::save_token(const nlohmann::json& token) {
  nlohmann::json stored = token;
  stored["expires_at"] = now_seconds() + stored.value("expires_in", 3600) - 60;

  if (token_.contains("refresh_token") && !stored.contains("refresh_token")) {
    stored["refresh_token"] = token_["refresh_token"];
  }

  std::filesystem::create_directories(token_cache_.parent_path());
  std::ofstream output(token_cache_);
  output << stored.dump(2);
  token_ = stored;
}

void SpotifyClient::raise_http_error(const HttpResponse& response, const std::string& context) const {
  throw std::runtime_error(context + " failed with HTTP " + std::to_string(response.status) + ": " + response.body);
}

nlohmann::json SpotifyClient::post_token(const std::map<std::string, std::string>& data) {
  const std::string credentials = client_id_ + ":" + client_secret_;
  const HttpResponse response = http_.request(
      "POST",
      kTokenUrl,
      {},
      data,
      {
          {"Authorization", "Basic " + util::base64_encode(credentials)},
          {"Content-Type", "application/x-www-form-urlencoded"},
      });

  if (response.status != 200) {
    raise_http_error(response, "Spotify token request");
  }
  return nlohmann::json::parse(response.body);
}

nlohmann::json SpotifyClient::authorize_interactive() {
  const auto redirect = parse_redirect_uri(redirect_uri_);
  const std::string state = util::random_token();

  OAuthCallbackServer callback(redirect.host, redirect.port, redirect.path, state);

  std::map<std::string, std::string> query{
      {"client_id", client_id_},
      {"response_type", "code"},
      {"redirect_uri", redirect_uri_},
      {"scope", kScope},
      {"state", state},
  };

  std::ostringstream auth_url;
  auth_url << kAuthUrl << '?';
  bool first = true;
  for (const auto& [key, value] : query) {
    if (!first) {
      auth_url << '&';
    }
    first = false;
    auth_url << util::url_encode(key) << '=' << util::url_encode(value);
  }

  std::cout << "Authorize Spotify in your browser:\n" << auth_url.str() << '\n';
  if (open_browser_) {
    open_browser(auth_url.str());
  }

  const std::string code = callback.wait_for_code();
  const nlohmann::json token = post_token({
      {"grant_type", "authorization_code"},
      {"code", code},
      {"redirect_uri", redirect_uri_},
  });
  save_token(token);
  return token_;
}

void SpotifyClient::refresh_access_token() {
  if (!token_.contains("refresh_token")) {
    authorize_interactive();
    return;
  }

  try {
    const nlohmann::json token = post_token({
        {"grant_type", "refresh_token"},
        {"refresh_token", token_["refresh_token"].get<std::string>()},
    });
    save_token(token);
  } catch (const std::exception&) {
    token_ = nlohmann::json::object();
    authorize_interactive();
  }
}

std::string SpotifyClient::valid_access_token() {
  if (token_.empty() || !token_.contains("access_token")) {
    authorize_interactive();
  }

  if (now_seconds() >= token_.value("expires_at", 0.0)) {
    refresh_access_token();
  }

  return token_["access_token"].get<std::string>();
}

std::optional<PlaybackArt> SpotifyClient::playback_art_from_json(const nlohmann::json& playback) const {
  if (!playback.contains("item") || playback["item"].is_null()) {
    return std::nullopt;
  }

  const nlohmann::json& item = playback["item"];
  const std::string item_type = item.value("type", "");
  nlohmann::json images;
  if (item_type == "track") {
    images = item.value("album", nlohmann::json::object()).value("images", nlohmann::json::array());
  } else {
    images = item.value("images", nlohmann::json::array());
  }

  if (!images.is_array() || images.empty()) {
    return std::nullopt;
  }

  const nlohmann::json* best = &images[0];
  for (const auto& candidate : images) {
    if (candidate.value("width", 0) > best->value("width", 0)) {
      best = &candidate;
    }
  }

  PlaybackArt art;
  art.image_url = best->value("url", "");
  if (art.image_url.empty()) {
    return std::nullopt;
  }

  if (item.contains("id") && !item["id"].is_null()) {
    art.key = item["id"].get<std::string>();
  } else if (item.contains("uri") && !item["uri"].is_null()) {
    art.key = item["uri"].get<std::string>();
  } else {
    art.key = art.image_url;
  }
  art.is_playing = playback.value("is_playing", false);
  return art;
}

std::optional<PlaybackArt> SpotifyClient::get_currently_playing() {
  const std::string token = valid_access_token();
  const HttpResponse response = http_.request(
      "GET",
      kCurrentlyPlayingUrl,
      {{"additional_types", "track,episode"}},
      {},
      {{"Authorization", "Bearer " + token}});

  if (response.status == 204) {
    return std::nullopt;
  }
  if (response.status == 401) {
    refresh_access_token();
    return get_currently_playing();
  }
  if (response.status == 429) {
    int retry_after = 5;
    if (response.headers.count("retry-after")) {
      retry_after = std::max(1, std::stoi(response.headers.at("retry-after")));
    }
    std::this_thread::sleep_for(std::chrono::seconds(retry_after));
    return std::nullopt;
  }
  if (response.status != 200) {
    raise_http_error(response, "Spotify currently-playing request");
  }

  return playback_art_from_json(nlohmann::json::parse(response.body));
}
