#pragma once

#include "image_renderer.hpp"

#include <cstdint>
#include <mutex>

class FramePreview {
 public:
  struct Snapshot {
    ImageBuffer pixels;
    int width = 0;
    int height = 0;
    uint64_t sequence = 0;
  };

  void update(const ImageBuffer& frame, int width, int height);
  void clear(int width, int height);
  Snapshot snapshot() const;

 private:
  mutable std::mutex mutex_;
  ImageBuffer pixels_;
  int width_ = 0;
  int height_ = 0;
  uint64_t sequence_ = 0;
};
