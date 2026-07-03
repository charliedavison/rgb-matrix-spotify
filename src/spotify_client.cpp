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
#include <system_error>
#include <thread>

#if defined(__linux__)
#include <sys/stat.h>
#include <unistd.h>
#endif

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

void fix_token_cache_permissions(const std::filesystem::path& path) {
#if defined(__linux__)
  if (geteuid() != 0) {
    return;
  }

  const char* sudo_uid = std::getenv("SUDO_UID");
  const char* sudo_gid = std::getenv("SUDO_GID");
  if (!sudo_uid || !sudo_gid) {
    return;
  }

  chown(path.c_str(), static_cast<uid_t>(std::stoul(sudo_uid)), static_cast<gid_t>(std::stoul(sudo_gid)));
  if (std::filesystem::is_directory(path)) {
    chmod(path.c_str(), S_IRUSR | S_IWUSR | S_IXUSR);
  } else {
    chmod(path.c_str(), S_IRUSR | S_IWUSR);
  }
#endif
  (void)path;
}

class UserCredentialScope {
 public:
  UserCredentialScope() {
#if defined(__linux__)
    if (geteuid() != 0) {
      return;
    }

    const char* sudo_uid = std::getenv("SUDO_UID");
    const char* sudo_gid = std::getenv("SUDO_GID");
    if (!sudo_uid || !sudo_gid) {
      return;
    }

    saved_euid_ = geteuid();
    saved_egid_ = getegid();
    const gid_t target_gid = static_cast<gid_t>(std::stoul(sudo_gid));
    const uid_t target_uid = static_cast<uid_t>(std::stoul(sudo_uid));
    if (setegid(target_gid) == 0 && seteuid(target_uid) == 0) {
      active_ = true;
    }
#endif
  }

  ~UserCredentialScope() {
#if defined(__linux__)
    if (!active_) {
      return;
    }
    seteuid(saved_euid_);
    setegid(saved_egid_);
#endif
  }

  bool active() const { return active_; }

 private:
  bool active_ = false;
#if defined(__linux__)
  uid_t saved_euid_ = 0;
  gid_t saved_egid_ = 0;
#endif
};

bool has_non_empty_string(const nlohmann::json& value, const char* key) {
  return value.contains(key) && value[key].is_string() && !value[key].get<std::string>().empty();
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
  ensure_token_cache_directory();
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
    std::cerr << "Spotify: unable to read token cache at " << token_cache_ << std::endl;
    token_ = nlohmann::json::object();
    return;
  }

  try {
    input >> token_;
  } catch (const std::exception& ex) {
    std::cerr << "Spotify: invalid token cache at " << token_cache_ << ": " << ex.what() << std::endl;
    token_ = nlohmann::json::object();
    return;
  }

  if (!token_.is_object() || !has_non_empty_string(token_, "access_token")) {
    token_ = nlohmann::json::object();
    return;
  }

  if (!has_non_empty_string(token_, "refresh_token")) {
    std::cerr << "Spotify: token cache at " << token_cache_
              << " is missing a refresh token; re-authorization is required." << std::endl;
    token_ = nlohmann::json::object();
    return;
  }

  if (!token_.contains("expires_at")) {
    if (token_.contains("expires_in")) {
      token_["expires_at"] = now_seconds() + token_.value("expires_in", 3600) - 60.0;
    } else {
      token_["expires_at"] = 0.0;
    }
  }
}

void SpotifyClient::clear_token_cache() {
  token_ = nlohmann::json::object();
  UserCredentialScope user_scope;
  std::error_code ec;
  std::filesystem::remove(token_cache_, ec);
}

void SpotifyClient::ensure_token_cache_directory() const {
  UserCredentialScope user_scope;
  const auto dir = token_cache_.parent_path();
  std::error_code ec;
  std::filesystem::create_directories(dir, ec);
  if (ec) {
    std::ostringstream message;
    message << "Failed to create Spotify token cache directory " << dir << ": " << ec.message();
    if (!user_scope.active()) {
      message << " (running as root without SUDO_UID; use ./run.sh or sudo -E)";
    } else {
      message << ". Try: sudo chown -R \"$USER:$USER\" \"" << dir << "\"";
    }
    throw std::runtime_error(message.str());
  }
}

void SpotifyClient::save_token(const nlohmann::json& token) {
  if (!token.is_object() || !has_non_empty_string(token, "access_token")) {
    throw std::runtime_error("Spotify token response missing access_token");
  }

  nlohmann::json stored = token;
  stored["expires_at"] = now_seconds() + stored.value("expires_in", 3600) - 60.0;

  if (has_non_empty_string(stored, "refresh_token")) {
    // Use the refresh token returned by Spotify when present.
  } else if (has_non_empty_string(token_, "refresh_token")) {
    stored["refresh_token"] = token_["refresh_token"];
  }

  if (!has_non_empty_string(stored, "refresh_token")) {
    throw std::runtime_error("Spotify token response missing refresh_token");
  }

  UserCredentialScope user_scope;
  ensure_token_cache_directory();

  const auto temp_path = token_cache_.string() + ".tmp";
  {
    std::ofstream output(temp_path, std::ios::trunc);
    if (!output) {
      throw std::runtime_error("Failed to open Spotify token cache for writing: " + temp_path);
    }
    output << stored.dump(2);
    output.flush();
    if (!output) {
      throw std::runtime_error("Failed to write Spotify token cache: " + temp_path);
    }
  }

  std::error_code ec;
  std::filesystem::rename(temp_path, token_cache_, ec);
  if (ec) {
    std::filesystem::remove(temp_path, ec);
    throw std::runtime_error("Failed to update Spotify token cache: " + ec.message());
  }

  if (!user_scope.active()) {
    fix_token_cache_permissions(token_cache_.parent_path());
    fix_token_cache_permissions(token_cache_);
  }
  token_ = stored;
}

void SpotifyClient::raise_http_error(const HttpResponse& response, const std::string& context) const {
  throw std::runtime_error(context + " failed with HTTP " + std::to_string(response.status) + ": " + response.body);
}

HttpResponse SpotifyClient::post_token_request(const std::map<std::string, std::string>& data) {
  const std::string credentials = client_id_ + ":" + client_secret_;
  return http_.request(
      "POST",
      kTokenUrl,
      {},
      data,
      {
          {"Authorization", "Basic " + util::base64_encode(credentials)},
          {"Content-Type", "application/x-www-form-urlencoded"},
      });
}

nlohmann::json SpotifyClient::post_token(const std::map<std::string, std::string>& data) {
  const HttpResponse response = post_token_request(data);
  if (response.status != 200) {
    raise_http_error(response, "Spotify token request");
  }
  return nlohmann::json::parse(response.body);
}

bool SpotifyClient::is_invalid_grant(const HttpResponse& response) const {
  if (response.status != 400) {
    return false;
  }

  try {
    const nlohmann::json body = nlohmann::json::parse(response.body);
    return body.value("error", "") == "invalid_grant";
  } catch (const std::exception&) {
    return false;
  }
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
  if (!has_non_empty_string(token_, "refresh_token")) {
    std::cerr << "Spotify: no refresh token available; re-authorization required." << std::endl;
    authorize_interactive();
    return;
  }

  const std::string refresh_token = token_["refresh_token"].get<std::string>();
  const std::map<std::string, std::string> request{
      {"grant_type", "refresh_token"},
      {"refresh_token", refresh_token},
      {"scope", kScope},
  };

  for (int attempt = 0; attempt < 3; ++attempt) {
    const HttpResponse response = post_token_request(request);

    if (response.status == 200) {
      try {
        save_token(nlohmann::json::parse(response.body));
        return;
      } catch (const std::exception& ex) {
        std::cerr << "Spotify: refreshed token but failed to save cache: " << ex.what() << std::endl;
        throw;
      }
    }

    if (is_invalid_grant(response)) {
      std::cerr << "Spotify: refresh token rejected; re-authorization required." << std::endl;
      clear_token_cache();
      authorize_interactive();
      return;
    }

    std::cerr << "Spotify: token refresh failed (HTTP " << response.status << "): " << response.body << std::endl;
    if (attempt + 1 < 3) {
      std::this_thread::sleep_for(std::chrono::seconds(2 * (attempt + 1)));
    }
  }

  throw std::runtime_error("Spotify token refresh failed after retries");
}

std::string SpotifyClient::valid_access_token() {
  if (token_.empty() || !has_non_empty_string(token_, "access_token")) {
    authorize_interactive();
  }

  if (now_seconds() >= token_.value("expires_at", 0.0)) {
    refresh_access_token();
  }

  return token_["access_token"].get<std::string>();
}

std::optional<PlaybackInfo> SpotifyClient::playback_from_json(const nlohmann::json& playback) const {
  if (!playback.contains("item") || playback["item"].is_null()) {
    return std::nullopt;
  }

  const nlohmann::json& item = playback["item"];
  const std::string item_type = item.value("type", "");

  PlaybackInfo info;
  info.title = item.value("name", "");
  info.progress_ms = playback.value("progress_ms", 0);
  info.duration_ms = item.value("duration_ms", 0);
  info.is_playing = playback.value("is_playing", false);
  info.is_podcast = item_type == "episode" || playback.value("currently_playing_type", "") == "episode";

  if (item_type == "track") {
    if (item.contains("artists") && item["artists"].is_array() && !item["artists"].empty()) {
      info.artist = item["artists"][0].value("name", "");
    }
  } else if (item_type == "episode") {
    info.artist = item.value("show", nlohmann::json::object()).value("name", "");
  }

  nlohmann::json images;
  if (item_type == "track") {
    images = item.value("album", nlohmann::json::object()).value("images", nlohmann::json::array());
  } else {
    images = item.value("images", nlohmann::json::array());
  }

  if (images.is_array() && !images.empty()) {
    const nlohmann::json* best = &images[0];
    for (const auto& candidate : images) {
      if (candidate.value("width", 0) > best->value("width", 0)) {
        best = &candidate;
      }
    }
    info.image_url = best->value("url", "");
  }

  if (item.contains("id") && !item["id"].is_null()) {
    info.key = item["id"].get<std::string>();
  } else if (item.contains("uri") && !item["uri"].is_null()) {
    info.key = item["uri"].get<std::string>();
  } else if (!info.title.empty()) {
    info.key = info.title;
  } else {
    info.key = "unknown";
  }

  return info;
}

std::optional<PlaybackInfo> SpotifyClient::get_currently_playing(int auth_retry) {
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
  if (response.status == 401 && auth_retry < 1) {
    token_["expires_at"] = 0.0;
    refresh_access_token();
    return get_currently_playing(auth_retry + 1);
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

  return playback_from_json(nlohmann::json::parse(response.body));
}
