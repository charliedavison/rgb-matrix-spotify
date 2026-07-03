#include "display.hpp"

#include <iostream>
#include <stdexcept>

#ifndef SPOTIFY_MATRIX_MOCK
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
    options.limit_refresh_rate_hz = config.limit_refresh_rate_hz;
    options.disable_hardware_pulsing = config.no_hardware_pulse;

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
    for (int y = 0; y < height; ++y) {
      for (int x = 0; x < width; ++x) {
        const util::Rgb& pixel = frame[y * width + x];
        canvas_->SetPixel(x, y, pixel.r, pixel.g, pixel.b);
      }
    }
    canvas_ = matrix_->SwapOnVSync(canvas_);
  }

  void clear() override {
    matrix_->Clear();
  }

 private:
  std::unique_ptr<rgb_matrix::RGBMatrix> matrix_;
  rgb_matrix::FrameCanvas* canvas_ = nullptr;
};

#endif

}  // namespace

std::unique_ptr<Display> create_display(const AppConfig& config) {
  if (!config.mock_output.empty()) {
    return std::make_unique<MockDisplay>(config.mock_output);
  }

#ifndef SPOTIFY_MATRIX_MOCK
  return std::make_unique<MatrixDisplay>(config);
#else
  throw std::runtime_error(
      "Matrix hardware support was not built. Rebuild without -DSPOTIFY_MATRIX_MOCK=ON or use --mock-output.");
#endif
}
