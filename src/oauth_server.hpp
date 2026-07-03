#pragma once

#include <string>

class OAuthCallbackServer {
 public:
  OAuthCallbackServer(std::string host, int port, std::string path, std::string expected_state);
  std::string wait_for_code();

 private:
  std::string host_;
  int port_;
  std::string path_;
  std::string expected_state_;
};
