#include "http_client.hpp"

#include "util.hpp"

#include <curl/curl.h>
#include <mutex>
#include <sstream>
#include <stdexcept>

namespace {

struct WriteContext {
  std::string* body;
};

struct HeaderContext {
  std::map<std::string, std::string>* headers;
};

std::string to_lower(std::string value) {
  for (auto& ch : value) {
    ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
  }
  return value;
}

}  // namespace

HttpClient::HttpClient() {
  static std::once_flag once;
  std::call_once(once, []() { curl_global_init(CURL_GLOBAL_DEFAULT); });
}

HttpClient::~HttpClient() = default;

size_t HttpClient::write_callback(char* ptr, size_t size, size_t nmemb, void* userdata) {
  const size_t total = size * nmemb;
  auto* context = static_cast<WriteContext*>(userdata);
  context->body->append(ptr, total);
  return total;
}

size_t HttpClient::header_callback(char* buffer, size_t size, size_t nitems, void* userdata) {
  const size_t total = size * nitems;
  auto* context = static_cast<HeaderContext*>(userdata);
  std::string line(buffer, total);
  line = util::trim(line);
  if (line.empty() || line.find(':') == std::string::npos) {
    return total;
  }

  const auto colon = line.find(':');
  const std::string key = to_lower(util::trim(line.substr(0, colon)));
  const std::string value = util::trim(line.substr(colon + 1));
  (*context->headers)[key] = value;
  return total;
}

std::string HttpClient::build_url(const std::string& url, const std::map<std::string, std::string>& params) {
  if (params.empty()) {
    return url;
  }

  std::ostringstream oss;
  oss << url;
  oss << (url.find('?') == std::string::npos ? '?' : '&');
  bool first = true;
  for (const auto& [key, value] : params) {
    if (!first) {
      oss << '&';
    }
    first = false;
    oss << util::url_encode(key) << '=' << util::url_encode(value);
  }
  return oss.str();
}

HttpResponse HttpClient::request(
    const std::string& method,
    const std::string& url,
    const std::map<std::string, std::string>& params,
    const std::map<std::string, std::string>& form_data,
    const std::map<std::string, std::string>& headers,
    double timeout_seconds) const {
  CURL* curl = curl_easy_init();
  if (!curl) {
    throw std::runtime_error("Failed to initialize libcurl");
  }

  const std::string final_url = build_url(url, params);
  HttpResponse response;
  WriteContext write_context{&response.body};
  HeaderContext header_context{&response.headers};

  curl_easy_setopt(curl, CURLOPT_URL, final_url.c_str());
  curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, method.c_str());
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &write_context);
  curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, header_callback);
  curl_easy_setopt(curl, CURLOPT_HEADERDATA, &header_context);
  curl_easy_setopt(curl, CURLOPT_TIMEOUT, static_cast<long>(timeout_seconds));
  curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);

  struct curl_slist* header_list = nullptr;
  for (const auto& [key, value] : headers) {
    const std::string header = key + ": " + value;
    header_list = curl_slist_append(header_list, header.c_str());
  }
  if (header_list) {
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, header_list);
  }

  std::string form_payload;
  if (!form_data.empty()) {
    bool first = true;
    for (const auto& [key, value] : form_data) {
      if (!first) {
        form_payload.push_back('&');
      }
      first = false;
      form_payload += util::url_encode(key);
      form_payload.push_back('=');
      form_payload += util::url_encode(value);
    }
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, form_payload.c_str());
  }

  const CURLcode code = curl_easy_perform(curl);
  if (code != CURLE_OK) {
    curl_slist_free_all(header_list);
    curl_easy_cleanup(curl);
    throw std::runtime_error(std::string("HTTP request failed: ") + curl_easy_strerror(code));
  }

  curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response.status);
  curl_slist_free_all(header_list);
  curl_easy_cleanup(curl);
  return response;
}
