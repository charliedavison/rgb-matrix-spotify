#include "frame_preview.hpp"

void FramePreview::update(const ImageBuffer& frame, int width, int height) {
  std::lock_guard lock(mutex_);
  pixels_ = frame;
  width_ = width;
  height_ = height;
  ++sequence_;
}

void FramePreview::clear(int width, int height) {
  std::lock_guard lock(mutex_);
  pixels_.assign(static_cast<std::size_t>(width * height), util::Rgb{});
  width_ = width;
  height_ = height;
  ++sequence_;
}

FramePreview::Snapshot FramePreview::snapshot() const {
  std::lock_guard lock(mutex_);
  return Snapshot{pixels_, width_, height_, sequence_};
}
