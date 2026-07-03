#pragma once

#include "http_client.hpp"
#include "util.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

using ImageBuffer = std::vector<util::Rgb>;

struct LoadedImage {
  int width = 0;
  int height = 0;
  ImageBuffer pixels;
};

LoadedImage download_image(const HttpClient& http, const std::string& url);
LoadedImage load_image_from_memory(const std::vector<uint8_t>& bytes);
LoadedImage fit_square(const LoadedImage& source, int size);
LoadedImage demo_album_art(int size);

ImageBuffer render_record(const LoadedImage* art, double angle_degrees, int size);
ImageBuffer render_idle(int size);
ImageBuffer render_test_pattern(int size, int offset);

void save_png(const std::filesystem::path& path, const ImageBuffer& pixels, int width, int height);
void render_preview_frames(const std::filesystem::path& directory);
