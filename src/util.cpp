#include "util.hpp"

#include <algorithm>
#include <cctype>
#include <curl/curl.h>
#include <random>
#include <sstream>

#if defined(__linux__) || defined(__APPLE__)
#include <pwd.h>
#include <unistd.h>
#endif

namespace util {

std::string base64_encode(const std::string& input) {
  static const char* kChars =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

  std::string output;
  output.reserve(((input.size() + 2) / 3) * 4);

  std::size_t i = 0;
  while (i + 2 < input.size()) {
    const uint32_t triple = (static_cast<uint8_t>(input[i]) << 16) |
                            (static_cast<uint8_t>(input[i + 1]) << 8) |
                            static_cast<uint8_t>(input[i + 2]);
    output.push_back(kChars[(triple >> 18) & 0x3F]);
    output.push_back(kChars[(triple >> 12) & 0x3F]);
    output.push_back(kChars[(triple >> 6) & 0x3F]);
    output.push_back(kChars[triple & 0x3F]);
    i += 3;
  }

  if (i < input.size()) {
    const uint32_t triple = static_cast<uint8_t>(input[i]) << 16;
    output.push_back(kChars[(triple >> 18) & 0x3F]);
    if (i + 1 < input.size()) {
      const uint32_t with_next = triple | (static_cast<uint8_t>(input[i + 1]) << 8);
      output.push_back(kChars[(with_next >> 12) & 0x3F]);
      output.push_back(kChars[(with_next >> 6) & 0x3F]);
      output.push_back('=');
    } else {
      output.push_back(kChars[(triple >> 12) & 0x3F]);
      output.push_back('=');
      output.push_back('=');
    }
  }

  return output;
}

std::string url_encode(const std::string& input) {
  CURL* curl = curl_easy_init();
  if (!curl) {
    return input;
  }

  char* encoded = curl_easy_escape(curl, input.c_str(), static_cast<int>(input.size()));
  if (!encoded) {
    curl_easy_cleanup(curl);
    return input;
  }

  const std::string result(encoded);
  curl_free(encoded);
  curl_easy_cleanup(curl);
  return result;
}

std::string random_token(std::size_t bytes) {
  std::random_device rd;
  std::vector<uint8_t> buffer(bytes);
  for (auto& byte : buffer) {
    byte = static_cast<uint8_t>(rd());
  }

  static const char* kChars =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

  std::string token;
  token.reserve(bytes + 8);
  for (const auto byte : buffer) {
    token.push_back(kChars[byte % 64]);
  }
  return token;
}

std::string trim(const std::string& value) {
  const auto start = std::find_if_not(value.begin(), value.end(), [](unsigned char c) {
    return std::isspace(c);
  });
  const auto end = std::find_if_not(value.rbegin(), value.rend(), [](unsigned char c) {
    return std::isspace(c);
  }).base();
  if (start >= end) {
    return {};
  }
  return std::string(start, end);
}

std::filesystem::path effective_home_directory() {
#if defined(__linux__) || defined(__APPLE__)
  if (geteuid() == 0) {
    if (const char* sudo_user = std::getenv("SUDO_USER"); sudo_user && *sudo_user) {
      if (const passwd* pw = getpwnam(sudo_user)) {
        return pw->pw_dir;
      }
    }
  }
#endif
  if (const char* home = std::getenv("HOME"); home && *home) {
    return home;
  }
  return std::filesystem::current_path();
}

std::filesystem::path expand_user_path(std::filesystem::path path) {
  const std::string raw = path.string();
  if (raw.empty() || raw[0] != '~') {
    return path;
  }
  if (raw.size() == 1) {
    return effective_home_directory();
  }
  if (raw[1] == '/') {
    return effective_home_directory() / raw.substr(2);
  }
  return path;
}

}  // namespace util
