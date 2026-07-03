# Spotify Matrix (C++)

Shows the current Spotify album art on a 64x64 RGB matrix as a circular record. The album art is the record surface itself: it is cropped to a disk, spun while Spotify reports playback as active, and left stopped at the current angle when paused.

This is a C++ port of [tnarla/spotify-matrix](https://github.com/tnarla/spotify-matrix). It uses Spotify's Web API `currently-playing` endpoint, not the browser-only Web Playback SDK. The first run opens Spotify OAuth, then stores a refresh token in `.cache/spotify_token.json`.

## Hardware

Tested target setup:

- Raspberry Pi Zero
- 64x64 RGB LED matrix panel
- Adafruit RGB Matrix Bonnet

## Quick start on the Pi

1. Copy your Spotify credentials into `.env`:

```bash
cp .env.example .env
# edit .env with your SPOTIFY_CLIENT_ID and SPOTIFY_CLIENT_SECRET
```

2. Run the setup script once:

```bash
./setup.sh
```

3. If this is the first install, authorize Spotify:

```bash
./bin/spotify-matrix --auth-only
```

4. Run the display:

```bash
./run.sh
```

For less flicker after the [GPIO 4→18 solder mod](https://github.com/hzeller/rpi-rgb-led-matrix#improving-flicker-hardware-patch) and disabling onboard audio:

```bash
echo "blacklist snd_bcm2835" | sudo tee /etc/modprobe.d/blacklist-rgb-matrix.conf
sudo reboot
./run-quality.sh
```

The setup script installs system packages, clones and builds `rpi-rgb-led-matrix`, builds this project, and installs the executable to `bin/spotify-matrix`. `run.sh` launches it with the Pi Zero + Adafruit bonnet defaults. `run-quality.sh` uses `adafruit-hat-pwm` hardware pulsing for smoother output.

## Dependencies

On the Pi:

```bash
sudo apt update
sudo apt install -y build-essential cmake libcurl4-openssl-dev
```

Clone the RGB matrix driver library:

```bash
git clone https://github.com/hzeller/rpi-rgb-led-matrix external/rpi-rgb-led-matrix
cd external/rpi-rgb-led-matrix
make -j$(nproc)
```

## Build

```bash
cmake -S . -B build -DRGBMATRIX_ROOT=$PWD/external/rpi-rgb-led-matrix
cmake --build build -j$(nproc)
```

Build the matrix library with `make` first if you have not run `./setup.sh`:

```bash
make -C external/rpi-rgb-led-matrix
```

For development on a machine without matrix hardware:

```bash
cmake -S . -B build -DSPOTIFY_MATRIX_MOCK=ON
cmake --build build -j$(nproc)
```

## Spotify setup

Copy the env template and fill in your app credentials:

```bash
cp .env.example .env
```

In the [Spotify developer dashboard](https://developer.spotify.com/dashboard), allowlist this redirect URI exactly:

```
http://127.0.0.1:8888/callback
```

For a headless Pi, forward the callback port from your computer:

```bash
ssh -L 8888:127.0.0.1:8888 pi@raspberrypi.local
```

Then run the program on the Pi and open the printed authorization URL in your local browser.

## Run on Pi Zero + Adafruit bonnet

This matches the working command from the original Python project:

```bash
sudo -E ./build/spotify-matrix \
  --rows 64 \
  --cols 64 \
  --chain-length 1 \
  --parallel 1 \
  --gpio-slowdown 4 \
  --no-hardware-pulse \
  --hardware-mapping adafruit-hat
```

Useful options:

```bash
sudo -E ./build/spotify-matrix \
  --hardware-mapping adafruit-hat \
  --gpio-slowdown 4 \
  --brightness 65
```

Authorize once and exit:

```bash
./build/spotify-matrix --auth-only
```

Render one PNG frame without matrix hardware:

```bash
./build/spotify-matrix --mock-output /tmp/spotify-matrix-frame.png --once
```

Render four local preview frames:

```bash
./build/spotify-matrix --preview-frames /tmp/spotify-matrix-preview
```

Show a moving color test pattern:

```bash
sudo -E ./build/spotify-matrix --test-pattern --hardware-mapping adafruit-hat --gpio-slowdown 4
```

## Files

- `src/main.cpp` - main loop and Spotify polling thread
- `src/spotify_client.cpp` - OAuth, token cache, currently-playing API
- `src/image_renderer.cpp` - album art download, vinyl record rendering
- `src/display.cpp` - RGB matrix and mock PNG output
- `.env` - local Spotify credentials, ignored by Git
- `.env.example` - template for recreating local config

## Reducing flicker

Biggest improvement: `./run-quality.sh` after the GPIO 4→18 solder mod and disabling onboard audio.

Without the hardware mod, `./run.sh` already applies software anti-flicker settings for a 64×64 Pi Zero panel:

- `--pwm-bits 8` — faster panel refresh (slightly fewer color steps)
- `--scan-mode 1` — interlaced scan, less visible flicker at lower refresh rates
- `--limit-refresh-rate-hz 90` — caps refresh to reduce timing jitter
- `--no-busy-waiting` — avoids starving the Pi Zero CPU while waiting for refresh

Try tuning further:

```bash
# Lower refresh cap if you still see shimmer
./run.sh --limit-refresh-rate-hz 80

# Faster refresh, rougher color
./run.sh --pwm-bits 7

# If you see ghosting/smear instead of flicker, increase PWM timing
./run.sh --pwm-lsb-nanoseconds 200
```

Stutter during animation is separate from panel flicker — lower `--fps` or raise `--poll-seconds` if the Pi Zero struggles to keep up.

## Notes

- The Pi Zero is tight on memory. This C++ build avoids Python/Pillow overhead from the original project, but image rotation still costs CPU at higher FPS.
- If the panel flickers or colors look wrong, try adjusting `--gpio-slowdown` and `--brightness`.
- `sudo` is required for GPIO access to the matrix bonnet.
