#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace util {

std::string base64_encode(const std::string& input);
std::string url_encode(const std::string& input);
std::string random_token(std::size_t bytes = 18);
std::string trim(const std::string& value);
std::filesystem::path effective_home_directory();
std::filesystem::path expand_user_path(std::filesystem::path path);

struct Rgb {
  uint8_t r = 0;
  uint8_t g = 0;
  uint8_t b = 0;
};

struct Rgba {
  uint8_t r = 0;
  uint8_t g = 0;
  uint8_t b = 0;
  uint8_t a = 255;
};

}  // namespace util
