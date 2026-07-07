#include "web_server.hpp"

#include "image_renderer.hpp"
#include "json.hpp"

#include <arpa/inet.h>
#include <cctype>
#include <cerrno>
#include <cstring>
#include <iostream>
#include <map>
#include <netdb.h>
#include <sstream>
#include <string_view>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>

namespace {

constexpr const char* kIndexHtml = R"(<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>Spotify Matrix</title>
  <style>
    body { font-family: system-ui, sans-serif; background: #111; color: #eee; max-width: 420px; margin: 2rem auto; padding: 0 1rem; }
    h1 { font-size: 1.4rem; margin-bottom: 0.25rem; }
    p { color: #aaa; margin-top: 0; }
    button { display: block; width: 100%; padding: 0.9rem 1rem; margin: 0.6rem 0; font-size: 1rem; border-radius: 10px; border: 1px solid #333; background: #222; color: #eee; cursor: pointer; }
    button.active { background: #1db954; border-color: #1db954; color: #fff; }
    #status { margin-top: 1rem; color: #8f8; min-height: 1.2rem; }
  </style>
</head>
<body>
  <h1>Spotify Matrix</h1>
  <p>Choose what to show on the LED panel.</p>
  <button id="mode-vinyl" type="button">Spinning vinyl</button>
  <button id="mode-nowplaying" type="button">Track info</button>
  <button id="mode-off" type="button">Off</button>
  <div id="status"></div>
  <p id="sim-link" style="display:none; margin-top:1.5rem;"><a href="/simulator" style="color:#1db954;">Open matrix simulator preview</a></p>
  <script>
    async function refresh() {
      const response = await fetch('/api/mode');
      const data = await response.json();
      document.getElementById('mode-vinyl').classList.toggle('active', data.mode === 'vinyl');
      document.getElementById('mode-nowplaying').classList.toggle('active', data.mode === 'nowplaying');
      document.getElementById('mode-off').classList.toggle('active', data.mode === 'off');
    }
    async function setMode(mode) {
      const response = await fetch('/api/mode', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ mode })
      });
      if (!response.ok) {
        document.getElementById('status').textContent = 'Failed to update mode';
        return;
      }
      document.getElementById('status').textContent = 'Updated';
      refresh();
    }
    document.getElementById('mode-vinyl').addEventListener('click', () => setMode('vinyl'));
    document.getElementById('mode-nowplaying').addEventListener('click', () => setMode('nowplaying'));
    document.getElementById('mode-off').addEventListener('click', () => setMode('off'));
    refresh();
    fetch('/api/simulator').then((response) => {
      if (response.ok) {
        document.getElementById('sim-link').style.display = 'block';
      }
    }).catch(() => {});
  </script>
</body>
</html>
)";

constexpr const char* kSimulatorHtml = R"(<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>Spotify Matrix Simulator</title>
  <style>
    body { font-family: system-ui, sans-serif; background: #0d0d0d; color: #eee; max-width: 720px; margin: 2rem auto; padding: 0 1rem; }
    h1 { font-size: 1.4rem; margin-bottom: 0.25rem; }
    p { color: #aaa; margin-top: 0; }
    a { color: #1db954; }
    .panel-wrap { display: flex; justify-content: center; margin: 1.5rem 0; }
    .panel {
      background: #1a1a1a;
      border: 2px solid #333;
      border-radius: 12px;
      padding: 1rem;
      box-shadow: 0 8px 32px rgba(0, 0, 0, 0.45);
    }
    canvas {
      display: block;
      image-rendering: pixelated;
      image-rendering: crisp-edges;
      background: #000;
    }
    .meta { text-align: center; color: #888; font-size: 0.9rem; margin-top: 0.75rem; }
    #status { text-align: center; color: #8f8; min-height: 1.2rem; margin-top: 0.5rem; }
  </style>
</head>
<body>
  <h1>Matrix simulator</h1>
  <p>Live preview of the 64×64 panel. <a href="/">Back to mode controls</a></p>
  <div class="panel-wrap">
    <div class="panel">
      <canvas id="matrix" width="512" height="512"></canvas>
      <div class="meta">64×64 RGB matrix · 8× scale · nearest-neighbor upsampling</div>
    </div>
  </div>
  <div id="status">Connecting…</div>
  <script>
    const canvas = document.getElementById('matrix');
    const ctx = canvas.getContext('2d');
    const status = document.getElementById('status');
    const pixelSize = 8;
    const panelSize = 64;
    const offscreen = document.createElement('canvas');
    offscreen.width = panelSize;
    offscreen.height = panelSize;
    const offCtx = offscreen.getContext('2d');
    let lastSequence = -1;
    let inFlight = false;

    function drawFrame(bitmap) {
      offCtx.drawImage(bitmap, 0, 0, panelSize, panelSize);
      ctx.clearRect(0, 0, canvas.width, canvas.height);
      ctx.drawImage(offscreen, 0, 0, canvas.width, canvas.height);
      ctx.strokeStyle = 'rgba(255, 255, 255, 0.08)';
      for (let i = 0; i <= panelSize; ++i) {
        const p = i * pixelSize;
        ctx.beginPath();
        ctx.moveTo(p, 0);
        ctx.lineTo(p, canvas.height);
        ctx.stroke();
        ctx.beginPath();
        ctx.moveTo(0, p);
        ctx.lineTo(canvas.width, p);
        ctx.stroke();
      }
    }

    async function poll() {
      if (inFlight) {
        return;
      }
      inFlight = true;
      try {
        const response = await fetch('/api/frame.png');
        if (!response.ok) {
          status.textContent = 'Waiting for frames…';
          return;
        }
        const sequence = Number(response.headers.get('X-Frame-Sequence') || '0');
        if (sequence === lastSequence) {
          return;
        }
        const blob = await response.blob();
        const bitmap = await createImageBitmap(blob);
        drawFrame(bitmap);
        bitmap.close();
        lastSequence = sequence;
        status.textContent = 'Live';
      } catch (error) {
        status.textContent = 'Preview disconnected';
      } finally {
        inFlight = false;
      }
    }

    setInterval(poll, 66);
    poll();
  </script>
</body>
</html>
)";

std::map<std::string, std::string> parse_headers(const std::string& request) {
  std::map<std::string, std::string> headers;
  std::istringstream stream(request);
  std::string line;
  std::getline(stream, line);
  while (std::getline(stream, line)) {
    if (line == "\r" || line.empty()) {
      break;
    }
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }
    const auto colon = line.find(':');
    if (colon == std::string::npos) {
      continue;
    }
    std::string key = line.substr(0, colon);
    std::string value = line.substr(colon + 1);
    while (!value.empty() && value.front() == ' ') {
      value.erase(value.begin());
    }
    for (auto& ch : key) {
      ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    headers[key] = value;
  }
  return headers;
}

std::string request_body(const std::string& request) {
  const auto pos = request.find("\r\n\r\n");
  if (pos == std::string::npos) {
    return {};
  }
  return request.substr(pos + 4);
}

void send_response(int client, int status, std::string_view status_text, const std::string& content_type, const std::string& body) {
  std::ostringstream response;
  response << "HTTP/1.1 " << status << ' ' << status_text << "\r\n"
           << "Content-Type: " << content_type << "\r\n"
           << "Connection: close\r\n"
           << "Content-Length: " << body.size() << "\r\n\r\n"
           << body;
  const auto payload = response.str();
  send(client, payload.c_str(), payload.size(), 0);
}

void send_response_with_headers(
    int client,
    int status,
    std::string_view status_text,
    const std::string& content_type,
    const std::string& body,
    const std::map<std::string, std::string>& extra_headers) {
  std::ostringstream response;
  response << "HTTP/1.1 " << status << ' ' << status_text << "\r\n"
           << "Content-Type: " << content_type << "\r\n"
           << "Connection: close\r\n";
  for (const auto& [key, value] : extra_headers) {
    response << key << ": " << value << "\r\n";
  }
  response << "Content-Length: " << body.size() << "\r\n\r\n" << body;
  const auto payload = response.str();
  send(client, payload.c_str(), payload.size(), 0);
}

}  // namespace

ControlWebServer::ControlWebServer(std::string host, int port, DisplayModeStore& modes, FramePreview* preview)
    : host_(std::move(host)), port_(port), modes_(modes), preview_(preview) {}

void ControlWebServer::run_until_stopped(const std::atomic<bool>& stop) {
  addrinfo hints{};
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_flags = AI_PASSIVE;

  addrinfo* result = nullptr;
  const std::string port_string = std::to_string(port_);
  if (getaddrinfo(host_.c_str(), port_string.c_str(), &hints, &result) != 0) {
    std::cerr << "Web UI: failed to bind " << host_ << ':' << port_ << std::endl;
    return;
  }

  int server_fd = -1;
  for (addrinfo* addr = result; addr != nullptr; addr = addr->ai_next) {
    server_fd = socket(addr->ai_family, addr->ai_socktype, addr->ai_protocol);
    if (server_fd < 0) {
      continue;
    }

    const int reuse = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    if (bind(server_fd, addr->ai_addr, addr->ai_addrlen) == 0 && listen(server_fd, 4) == 0) {
      break;
    }

    close(server_fd);
    server_fd = -1;
  }
  freeaddrinfo(result);

  if (server_fd < 0) {
    std::cerr << "Web UI: failed to listen on " << host_ << ':' << port_ << std::endl;
    return;
  }

  std::cout << "Web UI: open http://127.0.0.1:" << port_ << " to switch display modes";
  if (preview_) {
    std::cout << " (simulator preview at /simulator)";
  }
  std::cout << std::endl;

  while (!stop.load()) {
    fd_set readfds;
    FD_ZERO(&readfds);
    FD_SET(server_fd, &readfds);

    timeval timeout{};
    timeout.tv_sec = 0;
    timeout.tv_usec = 200000;

    const int ready = select(server_fd + 1, &readfds, nullptr, nullptr, &timeout);
    if (ready < 0) {
      if (errno == EINTR) {
        continue;
      }
      break;
    }
    if (ready == 0) {
      continue;
    }

    const int client = accept(server_fd, nullptr, nullptr);
    if (client < 0) {
      continue;
    }

    char buffer[8192];
    const ssize_t received = recv(client, buffer, sizeof(buffer) - 1, 0);
    if (received <= 0) {
      close(client);
      continue;
    }
    buffer[received] = '\0';

    const std::string request(buffer);
    const auto line_end = request.find("\r\n");
    const std::string request_line = line_end == std::string::npos ? request : request.substr(0, line_end);
    const auto method_end = request_line.find(' ');
    const auto path_end = request_line.find(' ', method_end + 1);
    if (method_end == std::string::npos || path_end == std::string::npos) {
      send_response(client, 400, "Bad Request", "text/plain", "Bad request");
      close(client);
      continue;
    }

    const std::string method = request_line.substr(0, method_end);
    const std::string path = request_line.substr(method_end + 1, path_end - method_end - 1);

    if (method == "GET" && path == "/") {
      send_response(client, 200, "OK", "text/html; charset=utf-8", kIndexHtml);
    } else if (method == "GET" && path == "/simulator") {
      if (!preview_) {
        send_response(client, 404, "Not Found", "text/plain", "Simulator preview is not enabled");
      } else {
        send_response(client, 200, "OK", "text/html; charset=utf-8", kSimulatorHtml);
      }
    } else if (method == "GET" && path == "/api/simulator") {
      if (!preview_) {
        send_response(client, 404, "Not Found", "application/json", R"({"enabled":false})");
      } else {
        send_response(client, 200, "OK", "application/json", R"({"enabled":true})");
      }
    } else if (method == "GET" && path == "/api/frame.png") {
      if (!preview_) {
        send_response(client, 404, "Not Found", "text/plain", "Simulator preview is not enabled");
      } else {
        const auto frame = preview_->snapshot();
        if (frame.width <= 0 || frame.height <= 0 || frame.pixels.empty()) {
          send_response(client, 204, "No Content", "text/plain", "");
        } else {
          const std::vector<uint8_t> png = encode_png(frame.pixels, frame.width, frame.height);
          send_response_with_headers(
              client,
              200,
              "OK",
              "image/png",
              std::string(reinterpret_cast<const char*>(png.data()), png.size()),
              {
                  {"X-Frame-Sequence", std::to_string(frame.sequence)},
                  {"Cache-Control", "no-store"},
              });
        }
      }
    } else if (method == "GET" && path == "/api/mode") {
      nlohmann::json payload{
          {"mode", display_mode_name(modes_.get())},
          {"label", display_mode_label(modes_.get())},
      };
      send_response(client, 200, "OK", "application/json", payload.dump());
    } else if (method == "POST" && path == "/api/mode") {
      try {
        const nlohmann::json body = nlohmann::json::parse(request_body(request));
        modes_.set(parse_display_mode(body.at("mode").get<std::string>()));
        nlohmann::json payload{
            {"mode", display_mode_name(modes_.get())},
            {"label", display_mode_label(modes_.get())},
        };
        send_response(client, 200, "OK", "application/json", payload.dump());
      } catch (const std::exception& ex) {
        send_response(client, 400, "Bad Request", "application/json", std::string(R"({"error":")") + ex.what() + "\"}");
      }
    } else {
      send_response(client, 404, "Not Found", "text/plain", "Not found");
    }

    close(client);
  }

  close(server_fd);
}
