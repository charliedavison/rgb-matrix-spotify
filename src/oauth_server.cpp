#include "oauth_server.hpp"

#include "util.hpp"

#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <iostream>
#include <map>
#include <netdb.h>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <sys/socket.h>
#include <unistd.h>

namespace {

std::map<std::string, std::string> parse_query(const std::string& query) {
  std::map<std::string, std::string> params;
  std::size_t start = 0;
  while (start < query.size()) {
    const auto amp = query.find('&', start);
    const auto part = query.substr(start, amp == std::string::npos ? std::string::npos : amp - start);
    const auto eq = part.find('=');
    if (eq != std::string::npos) {
      params[part.substr(0, eq)] = part.substr(eq + 1);
    }
    if (amp == std::string::npos) {
      break;
    }
    start = amp + 1;
  }
  return params;
}

std::string url_decode(const std::string& value) {
  std::string decoded;
  decoded.reserve(value.size());
  for (std::size_t i = 0; i < value.size(); ++i) {
    if (value[i] == '%' && i + 2 < value.size()) {
      const auto hex = value.substr(i + 1, 2);
      decoded.push_back(static_cast<char>(std::strtol(hex.c_str(), nullptr, 16)));
      i += 2;
    } else if (value[i] == '+') {
      decoded.push_back(' ');
    } else {
      decoded.push_back(value[i]);
    }
  }
  return decoded;
}

void send_http(int client, int status, std::string_view body) {
  std::ostringstream response;
  response << "HTTP/1.1 " << status << " OK\r\n"
           << "Content-Type: text/plain\r\n"
           << "Connection: close\r\n"
           << "Content-Length: " << body.size() << "\r\n\r\n"
           << body;
  const auto payload = response.str();
  send(client, payload.c_str(), payload.size(), 0);
}

}  // namespace

OAuthCallbackServer::OAuthCallbackServer(
    std::string host,
    int port,
    std::string path,
    std::string expected_state)
    : host_(std::move(host)),
      port_(port),
      path_(std::move(path)),
      expected_state_(std::move(expected_state)) {}

std::string OAuthCallbackServer::wait_for_code() {
  addrinfo hints{};
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_flags = AI_PASSIVE;

  addrinfo* result = nullptr;
  const std::string port_string = std::to_string(port_);
  if (getaddrinfo(host_.c_str(), port_string.c_str(), &hints, &result) != 0) {
    throw std::runtime_error("Failed to resolve OAuth callback host");
  }

  int server_fd = -1;
  for (addrinfo* addr = result; addr != nullptr; addr = addr->ai_next) {
    server_fd = socket(addr->ai_family, addr->ai_socktype, addr->ai_protocol);
    if (server_fd < 0) {
      continue;
    }

    const int reuse = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    if (bind(server_fd, addr->ai_addr, addr->ai_addrlen) == 0 &&
        listen(server_fd, 1) == 0) {
      break;
    }

    close(server_fd);
    server_fd = -1;
  }
  freeaddrinfo(result);

  if (server_fd < 0) {
    throw std::runtime_error("Failed to bind OAuth callback server on port " + port_string);
  }

  std::string code;
  std::string error;

  while (code.empty() && error.empty()) {
    const int client = accept(server_fd, nullptr, nullptr);
    if (client < 0) {
      if (errno == EINTR) {
        continue;
      }
      close(server_fd);
      throw std::runtime_error("OAuth callback accept failed");
    }

    char buffer[4096];
    const ssize_t received = recv(client, buffer, sizeof(buffer) - 1, 0);
    if (received <= 0) {
      close(client);
      continue;
    }
    buffer[received] = '\0';

    std::string request(buffer);
    const auto line_end = request.find("\r\n");
    const std::string request_line = line_end == std::string::npos ? request : request.substr(0, line_end);
    const auto method_end = request_line.find(' ');
    const auto path_end = request_line.find(' ', method_end + 1);
    if (method_end == std::string::npos || path_end == std::string::npos) {
      send_http(client, 400, "Bad request.");
      close(client);
      continue;
    }

    const std::string request_path = request_line.substr(method_end + 1, path_end - method_end - 1);
    const auto query_pos = request_path.find('?');
    const std::string path_only = query_pos == std::string::npos ? request_path : request_path.substr(0, query_pos);
    const std::string query = query_pos == std::string::npos ? "" : request_path.substr(query_pos + 1);

    if (path_only != path_) {
      send_http(client, 404, "Wrong callback path.");
      close(client);
      continue;
    }

    auto params = parse_query(query);
    for (auto& [key, value] : params) {
      value = url_decode(value);
    }

    if (params.count("state") && params["state"] != expected_state_) {
      send_http(client, 400, "State mismatch.");
      close(client);
      throw std::runtime_error("Spotify callback state did not match.");
    }

    if (params.count("error")) {
      error = params["error"];
      send_http(client, 400, "Spotify authorization failed.");
      close(client);
      break;
    }

    if (params.count("code")) {
      code = params["code"];
      send_http(client, 200, "Spotify authorization complete. You can close this tab.");
      close(client);
      break;
    }

    send_http(client, 400, "Missing authorization code.");
    close(client);
  }

  close(server_fd);

  if (!error.empty()) {
    throw std::runtime_error("Spotify authorization failed: " + error);
  }
  if (code.empty()) {
    throw std::runtime_error("Spotify authorization did not return a code.");
  }
  return code;
}
