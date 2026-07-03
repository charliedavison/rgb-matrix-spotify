#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace util {

std::string base64_encode(const std::string& input);
std::string url_encode(const std::string& input);
std::string random_token(std::size_t bytes = 18);
std::string trim(const std::string& value);

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
