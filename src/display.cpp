#include "display.hpp"

#include <algorithm>
#include <iostream>
#include <stdexcept>

#ifndef SPOTIFY_MATRIX_MOCK
#include "graphics.h"
#include "led-matrix.h"
#endif

namespace {

class MockDisplay final : public Display {
 public:
  explicit MockDisplay(std::filesystem::path output) : output_(std::move(output)) {}

  void show(const ImageBuffer& frame, int width, int height) override {
    save_png(output_, frame, width, height);
  }

  void clear() override {}

 private:
  std::filesystem::path output_;
};

class PreviewDisplay final : public Display {
 public:
  PreviewDisplay(FramePreview& preview, int width, int height) : preview_(preview), width_(width), height_(height) {}

  void show(const ImageBuffer& frame, int width, int height) override {
    if (brightness_ < 100) {
      const double scale = brightness_ / 100.0;
      ImageBuffer adjusted(frame.size());
      for (std::size_t i = 0; i < frame.size(); ++i) {
        adjusted[i].r = static_cast<uint8_t>(frame[i].r * scale);
        adjusted[i].g = static_cast<uint8_t>(frame[i].g * scale);
        adjusted[i].b = static_cast<uint8_t>(frame[i].b * scale);
      }
      preview_.update(adjusted, width, height);
      return;
    }
    preview_.update(frame, width, height);
  }

  void clear() override {
    preview_.clear(width_, height_);
  }

  void set_brightness(int brightness) override {
    brightness_ = std::max(1, std::min(100, brightness));
  }

 private:
  FramePreview& preview_;
  int width_;
  int height_;
  int brightness_ = 100;
};

#ifndef SPOTIFY_MATRIX_MOCK

class MatrixDisplay final : public Display {
 public:
  explicit MatrixDisplay(const AppConfig& config) {
    rgb_matrix::RGBMatrix::Options options;
    options.rows = config.rows;
    options.cols = config.cols;
    options.chain_length = config.chain_length;
    options.parallel = config.parallel;
    options.brightness = config.brightness;
    options.hardware_mapping = config.hardware_mapping.c_str();
    options.pwm_bits = config.pwm_bits;
    options.pwm_lsb_nanoseconds = config.pwm_lsb_nanoseconds;
    options.pwm_dither_bits = config.pwm_dither_bits;
    options.scan_mode = config.scan_mode;
    options.limit_refresh_rate_hz = config.limit_refresh_rate_hz;
    options.disable_hardware_pulsing = config.no_hardware_pulse;
    options.disable_busy_waiting = config.no_busy_waiting;

    rgb_matrix::RuntimeOptions runtime;
    runtime.gpio_slowdown = config.gpio_slowdown;

    matrix_.reset(rgb_matrix::RGBMatrix::CreateFromOptions(options, runtime));
    if (!matrix_) {
      throw std::runtime_error("Failed to initialize RGB matrix hardware");
    }
    canvas_ = matrix_->CreateFrameCanvas();
  }

  ~MatrixDisplay() override {
    if (matrix_) {
      matrix_->Clear();
    }
  }

  void show(const ImageBuffer& frame, int width, int height) override {
    const std::size_t pixel_count = static_cast<std::size_t>(width * height);
    if (upload_buffer_.size() != pixel_count) {
      upload_buffer_.resize(pixel_count);
    }

    for (std::size_t i = 0; i < pixel_count; ++i) {
      upload_buffer_[i] = rgb_matrix::Color{frame[i].r, frame[i].g, frame[i].b};
    }

    canvas_->SetPixels(0, 0, width, height, upload_buffer_.data());
    canvas_ = matrix_->SwapOnVSync(canvas_);
  }

  void clear() override {
    matrix_->Clear();
  }

  void set_brightness(int brightness) override {
    if (matrix_) {
      matrix_->SetBrightness(std::max(1, std::min(100, brightness)));
    }
  }

 private:
  std::unique_ptr<rgb_matrix::RGBMatrix> matrix_;
  rgb_matrix::FrameCanvas* canvas_ = nullptr;
  std::vector<rgb_matrix::Color> upload_buffer_;
};

#endif

}  // namespace

std::unique_ptr<Display> create_display(const AppConfig& config, FramePreview* preview) {
  if (config.simulate) {
    if (!preview) {
      throw std::runtime_error("Internal error: simulator mode requires a frame preview buffer");
    }
    const int size = std::min(config.rows, config.cols);
    return std::make_unique<PreviewDisplay>(*preview, size, size);
  }

  if (!config.mock_output.empty()) {
    return std::make_unique<MockDisplay>(config.mock_output);
  }

#ifndef SPOTIFY_MATRIX_MOCK
  return std::make_unique<MatrixDisplay>(config);
#else
  throw std::runtime_error(
      "Matrix hardware support was not built. Rebuild without -DSPOTIFY_MATRIX_MOCK=ON, use --simulate, or use --mock-output.");
#endif
}
