#include "image_renderer.hpp"

#include "stb_image.h"
#include "stb_image_write.h"

#include <algorithm>
#include <cmath>
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

void draw_filled_circle(std::vector<util::Rgba>& pixels, int size, int cx, int cy, int radius, util::Rgba color) {
  const int r2 = radius * radius;
  for (int y = std::max(0, cy - radius); y <= std::min(size - 1, cy + radius); ++y) {
    for (int x = std::max(0, cx - radius); x <= std::min(size - 1, cx + radius); ++x) {
      const int dx = x - cx;
      const int dy = y - cy;
      if (dx * dx + dy * dy <= r2) {
        pixels[y * size + x] = blend(pixels[y * size + x], color);
      }
    }
  }
}

void draw_circle_outline(
    std::vector<util::Rgba>& pixels,
    int size,
    int cx,
    int cy,
    int radius,
    util::Rgba color,
    int width) {
  for (int y = 0; y < size; ++y) {
    for (int x = 0; x < size; ++x) {
      const int dx = x - cx;
      const int dy = y - cy;
      const double distance = std::sqrt(static_cast<double>(dx * dx + dy * dy));
      if (std::abs(distance - radius) <= width * 0.5 + 0.5) {
        pixels[y * size + x] = blend(pixels[y * size + x], color);
      }
    }
  }
}

LoadedImage rotate_image(const LoadedImage& source, double angle_degrees) {
  LoadedImage rotated;
  rotated.width = source.width;
  rotated.height = source.height;
  rotated.pixels.assign(source.pixels.size(), util::Rgb{});

  const double radians = angle_degrees * M_PI / 180.0;
  const double cos_a = std::cos(radians);
  const double sin_a = std::sin(radians);
  const double cx = (source.width - 1) * 0.5;
  const double cy = (source.height - 1) * 0.5;

  for (int y = 0; y < source.height; ++y) {
    for (int x = 0; x < source.width; ++x) {
      const double dx = x - cx;
      const double dy = y - cy;
      const double src_x = cos_a * dx + sin_a * dy + cx;
      const double src_y = -sin_a * dx + cos_a * dy + cy;
      rotated.pixels[y * source.width + x] = sample_bilinear(source, src_x, src_y);
    }
  }
  return rotated;
}

std::vector<util::Rgba> to_rgba(const ImageBuffer& rgb) {
  std::vector<util::Rgba> rgba(rgb.size());
  for (std::size_t i = 0; i < rgb.size(); ++i) {
    rgba[i] = util::Rgba{rgb[i].r, rgb[i].g, rgb[i].b, 255};
  }
  return rgba;
}

ImageBuffer to_rgb(const std::vector<util::Rgba>& rgba) {
  ImageBuffer rgb(rgba.size());
  for (std::size_t i = 0; i < rgba.size(); ++i) {
    rgb[i] = util::Rgb{rgba[i].r, rgba[i].g, rgba[i].b};
  }
  return rgb;
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

ImageBuffer render_record(const LoadedImage* art, double angle_degrees, int size) {
  ImageBuffer frame(static_cast<size_t>(size * size), util::Rgb{0, 0, 0});
  if (!art) {
    return frame;
  }

  const int margin = std::max(2, size / 32);
  const int disc_size = size - margin * 2;
  const LoadedImage square = fit_square(*art, disc_size);
  const LoadedImage rotated = rotate_image(square, angle_degrees);

  auto rgba = to_rgba(frame);
  const double center = (disc_size - 1) * 0.5;
  const double radius = center;

  for (int y = 0; y < disc_size; ++y) {
    for (int x = 0; x < disc_size; ++x) {
      const double dx = x - center;
      const double dy = y - center;
      if (dx * dx + dy * dy > radius * radius) {
        continue;
      }
      const int out_x = x + margin;
      const int out_y = y + margin;
      const util::Rgb& pixel = rotated.pixels[y * disc_size + x];
      rgba[out_y * size + out_x] = util::Rgba{pixel.r, pixel.g, pixel.b, 255};
    }
  }

  const int outline_width = std::max(1, size / 32);
  draw_circle_outline(rgba, size, size / 2, size / 2, size / 2 - margin, util::Rgba{6, 6, 6, 255}, outline_width);

  const int label_radius = std::max(5, size / 11);
  draw_filled_circle(rgba, size, size / 2, size / 2, label_radius, util::Rgba{16, 16, 16, 210});
  draw_circle_outline(rgba, size, size / 2, size / 2, label_radius, util::Rgba{220, 220, 220, 90}, 1);

  const int hole_radius = std::max(2, size / 25);
  draw_filled_circle(rgba, size, size / 2, size / 2, hole_radius, util::Rgba{0, 0, 0, 255});

  return to_rgb(rgba);
}

ImageBuffer render_idle(int size) {
  auto rgba = to_rgba(ImageBuffer(static_cast<size_t>(size * size), util::Rgb{0, 0, 0}));
  const int margin = std::max(2, size / 32);
  const int center = size / 2;
  const int radius = std::max(3, size / 18);

  draw_circle_outline(rgba, size, center, center, size / 2 - margin, util::Rgba{55, 55, 55, 255}, 2);
  draw_filled_circle(rgba, size, center, center, radius, util::Rgba{18, 18, 18, 255});
  return to_rgb(rgba);
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

void save_png(const std::filesystem::path& path, const ImageBuffer& pixels, int width, int height) {
  std::filesystem::create_directories(path.parent_path());
  std::vector<uint8_t> raw(static_cast<size_t>(width * height * 3));
  for (int i = 0; i < width * height; ++i) {
    raw[i * 3] = pixels[i].r;
    raw[i * 3 + 1] = pixels[i].g;
    raw[i * 3 + 2] = pixels[i].b;
  }
  if (!stbi_write_png(path.string().c_str(), width, height, 3, raw.data(), width * 3)) {
    throw std::runtime_error("Failed to write PNG: " + path.string());
  }
}

void render_preview_frames(const std::filesystem::path& directory) {
  std::filesystem::create_directories(directory);
  const LoadedImage art = demo_album_art(96);
  const double angles[] = {0, 45, 90, 135};
  for (int index = 0; index < 4; ++index) {
    const ImageBuffer frame = render_record(&art, angles[index], 64);
    std::ostringstream name;
    name << "album-disk-" << std::setw(2) << std::setfill('0') << index << ".png";
    save_png(directory / name.str(), frame, 64, 64);
  }
}
