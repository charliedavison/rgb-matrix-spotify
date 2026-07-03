#pragma once

#include <map>
#include <string>
#include <vector>

struct HttpResponse {
  long status = 0;
  std::string body;
  std::map<std::string, std::string> headers;
};

class HttpClient {
 public:
  HttpClient();
  ~HttpClient();

  HttpResponse request(
      const std::string& method,
      const std::string& url,
      const std::map<std::string, std::string>& params = {},
      const std::map<std::string, std::string>& form_data = {},
      const std::map<std::string, std::string>& headers = {},
      double timeout_seconds = 10.0) const;

 private:
  static size_t write_callback(char* ptr, size_t size, size_t nmemb, void* userdata);
  static size_t header_callback(char* buffer, size_t size, size_t nitems, void* userdata);
  static std::string build_url(const std::string& url, const std::map<std::string, std::string>& params);
};
