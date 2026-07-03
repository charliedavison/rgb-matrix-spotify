#pragma once

#include "display_mode.hpp"

#include <atomic>
#include <string>

class ControlWebServer {
 public:
  ControlWebServer(std::string host, int port, DisplayModeStore& modes);
  void run_until_stopped(const std::atomic<bool>& stop);

 private:
  std::string host_;
  int port_;
  DisplayModeStore& modes_;
};
