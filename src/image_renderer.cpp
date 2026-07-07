#include "image_renderer.hpp"

#include "stb_image.h"
#include "stb_image_write.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace {

int channel_value(const util::Rgb& color, int channel) {
  switch (channel) {
    case 0:
      return color.r;
    case 1:
      return color.g;
    default:
      return color.b;
  }
}

void set_channel(util::Rgb& color, int channel, uint8_t value) {
  switch (channel) {
    case 0:
      color.r = value;
      break;
    case 1:
      color.g = value;
      break;
    default:
      color.b = value;
      break;
  }
}

util::Rgb sample_bilinear(const LoadedImage& image, double x, double y) {
  x = std::clamp(x, 0.0, static_cast<double>(image.width - 1));
  y = std::clamp(y, 0.0, static_cast<double>(image.height - 1));

  const int x0 = static_cast<int>(std::floor(x));
  const int y0 = static_cast<int>(std::floor(y));
  const int x1 = std::min(x0 + 1, image.width - 1);
  const int y1 = std::min(y0 + 1, image.height - 1);
  const double tx = x - x0;
  const double ty = y - y0;

  const auto& c00 = image.pixels[y0 * image.width + x0];
  const auto& c10 = image.pixels[y0 * image.width + x1];
  const auto& c01 = image.pixels[y1 * image.width + x0];
  const auto& c11 = image.pixels[y1 * image.width + x1];

  util::Rgb out;
  for (int channel = 0; channel < 3; ++channel) {
    const double top = (1.0 - tx) * channel_value(c00, channel) + tx * channel_value(c10, channel);
    const double bottom = (1.0 - tx) * channel_value(c01, channel) + tx * channel_value(c11, channel);
    const double value = (1.0 - ty) * top + ty * bottom;
    set_channel(out, channel, static_cast<uint8_t>(std::clamp(value, 0.0, 255.0)));
  }
  return out;
}

util::Rgb sample_nearest(const LoadedImage& image, int x, int y) {
  x = std::clamp(x, 0, image.width - 1);
  y = std::clamp(y, 0, image.height - 1);
  return image.pixels[y * image.width + x];
}

util::Rgba blend(util::Rgba dst, util::Rgba src) {
  const double alpha = src.a / 255.0;
  const double inv = 1.0 - alpha;
  return util::Rgba{
      static_cast<uint8_t>(src.r * alpha + dst.r * inv),
      static_cast<uint8_t>(src.g * alpha + dst.g * inv),
      static_cast<uint8_t>(src.b * alpha + dst.b * inv),
      255,
  };
}

void draw_filled_circle(ImageBuffer& pixels, int size, int cx, int cy, int radius, util::Rgb color) {
  const int r2 = radius * radius;
  for (int y = std::max(0, cy - radius); y <= std::min(size - 1, cy + radius); ++y) {
    for (int x = std::max(0, cx - radius); x <= std::min(size - 1, cx + radius); ++x) {
      const int dx = x - cx;
      const int dy = y - cy;
      if (dx * dx + dy * dy <= r2) {
        pixels[y * size + x] = color;
      }
    }
  }
}

void draw_filled_circle_alpha(
    ImageBuffer& pixels,
    int size,
    int cx,
    int cy,
    int radius,
    util::Rgba color) {
  const int r2 = radius * radius;
  for (int y = std::max(0, cy - radius); y <= std::min(size - 1, cy + radius); ++y) {
    for (int x = std::max(0, cx - radius); x <= std::min(size - 1, cx + radius); ++x) {
      const int dx = x - cx;
      const int dy = y - cy;
      if (dx * dx + dy * dy <= r2) {
        const util::Rgb& dst = pixels[y * size + x];
        const util::Rgba blended = blend(util::Rgba{dst.r, dst.g, dst.b, 255}, color);
        pixels[y * size + x] = util::Rgb{blended.r, blended.g, blended.b};
      }
    }
  }
}

void draw_ring(ImageBuffer& pixels, int size, int cx, int cy, int radius, int width, util::Rgb color) {
  const int outer = radius + width;
  const int inner = std::max(0, radius - width);
  const int outer2 = outer * outer;
  const int inner2 = inner * inner;
  for (int y = std::max(0, cy - outer); y <= std::min(size - 1, cy + outer); ++y) {
    for (int x = std::max(0, cx - outer); x <= std::min(size - 1, cx + outer); ++x) {
      const int dx = x - cx;
      const int dy = y - cy;
      const int dist2 = dx * dx + dy * dy;
      if (dist2 <= outer2 && dist2 >= inner2) {
        pixels[y * size + x] = color;
      }
    }
  }
}

}  // namespace

LoadedImage load_image_from_memory(const std::vector<uint8_t>& bytes) {
  int width = 0;
  int height = 0;
  int channels = 0;
  unsigned char* data = stbi_load_from_memory(
      bytes.data(),
      static_cast<int>(bytes.size()),
      &width,
      &height,
      &channels,
      3);
  if (!data) {
    throw std::runtime_error("Failed to decode image");
  }

  LoadedImage image;
  image.width = width;
  image.height = height;
  image.pixels.resize(static_cast<size_t>(width * height));
  for (int i = 0; i < width * height; ++i) {
    image.pixels[i] = util::Rgb{data[i * 3], data[i * 3 + 1], data[i * 3 + 2]};
  }
  stbi_image_free(data);
  return image;
}

LoadedImage download_image(const HttpClient& http, const std::string& url) {
  const HttpResponse response = http.request("GET", url);
  if (response.status != 200) {
    throw std::runtime_error("Image download failed with HTTP " + std::to_string(response.status));
  }

  std::vector<uint8_t> bytes(response.body.begin(), response.body.end());
  return load_image_from_memory(bytes);
}

LoadedImage fit_square(const LoadedImage& source, int size) {
  LoadedImage fitted;
  fitted.width = size;
  fitted.height = size;
  fitted.pixels.assign(static_cast<size_t>(size * size), util::Rgb{});

  const double scale = std::max(
      static_cast<double>(size) / source.width,
      static_cast<double>(size) / source.height);
  const double scaled_w = source.width * scale;
  const double scaled_h = source.height * scale;
  const double offset_x = (size - scaled_w) * 0.5;
  const double offset_y = (size - scaled_h) * 0.5;

  for (int y = 0; y < size; ++y) {
    for (int x = 0; x < size; ++x) {
      const double src_x = (x - offset_x) / scale;
      const double src_y = (y - offset_y) / scale;
      if (src_x >= 0 && src_y >= 0 && src_x <= source.width - 1 && src_y <= source.height - 1) {
        fitted.pixels[y * size + x] = sample_bilinear(source, src_x, src_y);
      }
    }
  }
  return fitted;
}

LoadedImage demo_album_art(int size) {
  LoadedImage image;
  image.width = size;
  image.height = size;
  image.pixels.assign(static_cast<size_t>(size * size), util::Rgb{18, 18, 18});

  const int half = size / 2;
  for (int y = 0; y < size; ++y) {
    for (int x = 0; x < size; ++x) {
      util::Rgb color{18, 18, 18};
      if (x < half && y < half) {
        color = util::Rgb{238, 70, 60};
      } else if (x >= half && y < half) {
        color = util::Rgb{245, 180, 40};
      } else if (x < half && y >= half) {
        color = util::Rgb{35, 150, 235};
      } else {
        color = util::Rgb{65, 185, 95};
      }
      image.pixels[y * size + x] = color;
    }
  }
  return image;
}

void RecordRenderer::prepare(const LoadedImage* art, int size) {
  size_ = size;
  margin_ = std::max(2, size / 32);
  disc_size_ = size - margin_ * 2;
  disc_center_ = size / 2;
  disc_radius_ = disc_size_ / 2;
  label_radius_ = std::max(5, size / 11);
  disc_center_f_ = (disc_size_ - 1) * 0.5;

  if (!art) {
    has_art_ = false;
    fitted_art_ = {};
    overlay_.clear();
    return;
  }

  fitted_art_ = fit_square(*art, disc_size_);
  build_overlay();
  has_art_ = true;
}

void RecordRenderer::build_overlay() {
  overlay_.assign(static_cast<size_t>(size_ * size_), util::Rgb{0, 0, 0});

  const int outline_width = std::max(1, size_ / 32);
  draw_ring(overlay_, size_, disc_center_, disc_center_, disc_radius_, outline_width, util::Rgb{6, 6, 6});
  draw_filled_circle_alpha(
      overlay_,
      size_,
      disc_center_,
      disc_center_,
      label_radius_,
      util::Rgba{16, 16, 16, 210});
  draw_ring(overlay_, size_, disc_center_, disc_center_, label_radius_, 1, util::Rgb{220, 220, 220});

  const int hole_radius = std::max(2, size_ / 25);
  draw_filled_circle(overlay_, size_, disc_center_, disc_center_, hole_radius, util::Rgb{0, 0, 0});
}

const ImageBuffer& RecordRenderer::render(double angle_degrees) const {
  if (frame_buffer_.size() != overlay_.size()) {
    frame_buffer_ = overlay_;
  } else {
    std::memcpy(frame_buffer_.data(), overlay_.data(), overlay_.size() * sizeof(util::Rgb));
  }

  if (!has_art_) {
    return frame_buffer_;
  }

  const double radians = angle_degrees * M_PI / 180.0;
  const double cos_a = std::cos(radians);
  const double sin_a = std::sin(radians);
  const int label_r2 = label_radius_ * label_radius_;
  const int disc_r2 = disc_radius_ * disc_radius_;

  const int y0 = disc_center_ - disc_radius_;
  const int y1 = disc_center_ + disc_radius_;
  const int x0 = disc_center_ - disc_radius_;
  const int x1 = disc_center_ + disc_radius_;

  for (int y = y0; y <= y1; ++y) {
    for (int x = x0; x <= x1; ++x) {
      const int dx = x - disc_center_;
      const int dy = y - disc_center_;
      const int dist2 = dx * dx + dy * dy;
      if (dist2 > disc_r2 || dist2 <= label_r2) {
        continue;
      }

      const double src_x = cos_a * dx + sin_a * dy + disc_center_f_;
      const double src_y = -sin_a * dx + cos_a * dy + disc_center_f_;
      frame_buffer_[y * size_ + x] =
          sample_nearest(fitted_art_, static_cast<int>(std::lround(src_x)), static_cast<int>(std::lround(src_y)));
    }
  }

  return frame_buffer_;
}

ImageBuffer render_record(const LoadedImage* art, double angle_degrees, int size) {
  RecordRenderer renderer;
  renderer.prepare(art, size);
  return renderer.render(angle_degrees);
}

ImageBuffer render_idle(int size) {
  ImageBuffer frame(static_cast<size_t>(size * size), util::Rgb{0, 0, 0});
  const int margin = std::max(2, size / 32);
  const int center = size / 2;
  const int radius = std::max(3, size / 18);

  draw_ring(frame, size, center, center, size / 2 - margin, 2, util::Rgb{55, 55, 55});
  draw_filled_circle(frame, size, center, center, radius, util::Rgb{18, 18, 18});
  return frame;
}

ImageBuffer render_test_pattern(int size, int offset) {
  ImageBuffer frame(static_cast<size_t>(size * size), util::Rgb{0, 0, 0});
  const util::Rgb colors[] = {
      {255, 0, 0},     {255, 160, 0}, {255, 255, 0}, {0, 255, 0},
      {0, 120, 255},   {80, 0, 255},    {255, 255, 255}, {0, 0, 0},
  };
  const int color_count = static_cast<int>(sizeof(colors) / sizeof(colors[0]));
  const int stripe_width = std::max(1, size / color_count);

  for (int y = 0; y < size; ++y) {
    for (int x = 0; x < size; ++x) {
      const int shifted = (x - offset + size) % size;
      const int index = (shifted / stripe_width) % color_count;
      frame[y * size + x] = colors[index];
    }
  }

  for (int x = 0; x < size; ++x) {
    frame[x] = util::Rgb{255, 255, 255};
    frame[(size - 1) * size + x] = util::Rgb{255, 255, 255};
    frame[x * size] = util::Rgb{255, 255, 255};
    frame[x * size + (size - 1)] = util::Rgb{255, 255, 255};
  }
  return frame;
}

namespace {

std::vector<uint8_t> pixels_to_rgb24(const ImageBuffer& pixels, int width, int height) {
  std::vector<uint8_t> raw(static_cast<std::size_t>(width * height * 3));
  for (int i = 0; i < width * height; ++i) {
    raw[static_cast<std::size_t>(i) * 3] = pixels[static_cast<std::size_t>(i)].r;
    raw[static_cast<std::size_t>(i) * 3 + 1] = pixels[static_cast<std::size_t>(i)].g;
    raw[static_cast<std::size_t>(i) * 3 + 2] = pixels[static_cast<std::size_t>(i)].b;
  }
  return raw;
}

struct PngBuffer {
  std::vector<uint8_t> bytes;
};

void append_png_bytes(void* context, void* data, int size) {
  auto* buffer = static_cast<PngBuffer*>(context);
  const auto* bytes = static_cast<const uint8_t*>(data);
  buffer->bytes.insert(buffer->bytes.end(), bytes, bytes + size);
}

}  // namespace

void save_png(const std::filesystem::path& path, const ImageBuffer& pixels, int width, int height) {
  std::filesystem::create_directories(path.parent_path());
  const std::vector<uint8_t> raw = pixels_to_rgb24(pixels, width, height);
  if (!stbi_write_png(path.string().c_str(), width, height, 3, raw.data(), width * 3)) {
    throw std::runtime_error("Failed to write PNG: " + path.string());
  }
}

std::vector<uint8_t> encode_png(const ImageBuffer& pixels, int width, int height) {
  const std::vector<uint8_t> raw = pixels_to_rgb24(pixels, width, height);
  PngBuffer buffer;
  if (!stbi_write_png_to_func(append_png_bytes, &buffer, width, height, 3, raw.data(), width * 3)) {
    throw std::runtime_error("Failed to encode PNG frame");
  }
  return buffer.bytes;
}

void render_preview_frames(const std::filesystem::path& directory) {
  std::filesystem::create_directories(directory);
  const LoadedImage art = demo_album_art(96);
  RecordRenderer renderer;
  renderer.prepare(&art, 64);
  const double angles[] = {0, 45, 90, 135};
  for (int index = 0; index < 4; ++index) {
    const ImageBuffer frame = renderer.render(angles[index]);
    std::ostringstream name;
    name << "album-disk-" << std::setw(2) << std::setfill('0') << index << ".png";
    save_png(directory / name.str(), frame, 64, 64);
  }
}
