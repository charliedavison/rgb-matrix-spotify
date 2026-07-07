#pragma once

#include "config.hpp"
#include "frame_preview.hpp"
#include "image_renderer.hpp"

#include <filesystem>
#include <memory>

class Display {
 public:
  virtual ~Display() = default;
  virtual void show(const ImageBuffer& frame, int width, int height) = 0;
  virtual void clear() = 0;
  virtual void set_brightness(int brightness) {}
};

std::unique_ptr<Display> create_display(const AppConfig& config, FramePreview* preview = nullptr);
