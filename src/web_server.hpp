#pragma once

#include "display_mode.hpp"
#include "frame_preview.hpp"

#include <atomic>
#include <string>

class ControlWebServer {
 public:
  ControlWebServer(std::string host, int port, DisplayModeStore& modes, FramePreview* preview = nullptr);
  void run_until_stopped(const std::atomic<bool>& stop);

 private:
  std::string host_;
  int port_;
  DisplayModeStore& modes_;
  FramePreview* preview_;
};
