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

class RecordRenderer {
 public:
  void prepare(const LoadedImage* art, int size);
  const ImageBuffer& render(double angle_degrees) const;
  bool has_art() const { return has_art_; }

 private:
  void build_overlay();

  bool has_art_ = false;
  int size_ = 0;
  int disc_size_ = 0;
  int margin_ = 0;
  int disc_center_ = 0;
  int disc_radius_ = 0;
  int label_radius_ = 0;
  double disc_center_f_ = 0.0;
  LoadedImage fitted_art_;
  ImageBuffer overlay_;
  mutable ImageBuffer frame_buffer_;
};

ImageBuffer render_record(const LoadedImage* art, double angle_degrees, int size);
ImageBuffer render_idle(int size);
ImageBuffer render_test_pattern(int size, int offset);

void save_png(const std::filesystem::path& path, const ImageBuffer& pixels, int width, int height);
void render_preview_frames(const std::filesystem::path& directory);
