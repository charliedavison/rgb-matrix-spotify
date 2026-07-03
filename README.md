# Spotify Matrix (C++)

Shows the current Spotify album art on a 64×64 RGB matrix as a spinning vinyl record, or as scrolling track info with a progress bar. Album art is cropped to a disk and spun while Spotify reports playback as active; it stops at the current angle when paused.

This is a C++ port of [tnarla/spotify-matrix](https://github.com/tnarla/spotify-matrix). It uses Spotify's Web API `currently-playing` endpoint, not the browser-only Web Playback SDK. The first run opens Spotify OAuth, then stores a refresh token at `~/.cache/rgb-spotify/spotify_token.json`.

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
./build/spotify-matrix --auth-only --token-cache ~/.cache/rgb-spotify/spotify_token.json
```

4. Run the display:

```bash
./run.sh
```

5. Switch display modes from your phone or laptop on the same network:

```bash
http://<pi-ip-address>:8080
```

The web UI lets you choose:

- **Spinning vinyl** — album art as a rotating record
- **Track info** — artist, title, elapsed/total time, progress bar (long titles scroll)
- **Off** — blank panel until you pick another mode

Podcasts are detected automatically and always use the track-info layout (show name, episode title, purple `POD` badge, slower scroll) even when vinyl mode is selected.

`run.sh` also enables a night dim schedule and idle auto-off by default (see [Schedule](#schedule) below). Pass extra flags after `./run.sh` to override them, e.g. `./run.sh --idle-off-minutes 0`.

For less flicker after the [GPIO 4→18 solder mod](https://github.com/hzeller/rpi-rgb-led-matrix#improving-flicker-hardware-patch) and disabling onboard audio:

```bash
echo "blacklist snd_bcm2835" | sudo tee /etc/modprobe.d/blacklist-rgb-matrix.conf
sudo reboot
./run-quality.sh
```

The setup script installs system packages, clones and builds `rpi-rgb-led-matrix`, builds this project, and installs the executable to `bin/spotify-matrix`.

- **`run.sh`** — Pi Zero + Adafruit bonnet defaults, software anti-flicker settings, night dim, and idle auto-off
- **`run-quality.sh`** — same as above but uses `adafruit-hat-pwm` hardware pulsing (requires GPIO mod + audio blacklist)

## Display modes

| Mode | What you see |
|---|---|
| Spinning vinyl | Album art cropped to a disk; spins while playing, stops when paused |
| Track info | Scrolling artist and title, elapsed/total time, progress bar |
| Off | Panel cleared (manual via web UI) |

**Podcasts:** When Spotify reports an episode, the display switches to a podcast layout automatically: show name on the first line, episode title on the second, a purple `POD` badge, purple progress bar, and slower scrolling. Vinyl mode is not used for episodes.

## Schedule

Local-time rules to keep the panel from staying bright all night or when nothing is playing. Configured via CLI flags; `run.sh` enables sensible defaults.

| Flag | Default in `run.sh` | Description |
|---|---|---|
| `--night-start HH:MM` | `23:00` | Start of the night window (local time). Setting this or `--night-end` enables the night schedule. |
| `--night-end HH:MM` | `07:00` | End of the night window. Windows that cross midnight (e.g. 23:00→07:00) are supported. |
| `--night-brightness N` | `12` | Brightness during the night window (1–100). Use `0` to turn the panel fully off at night instead of dimming. |
| `--idle-off-minutes N` | `30` | Turn the panel off after *N* minutes with nothing playing on Spotify. Resumes automatically when playback starts. Use `0` to disable. |

Examples:

```bash
# Disable all scheduling
./run.sh --idle-off-minutes 0 --night-start 00:00 --night-end 00:00

# Full off at night instead of dim
./run.sh --night-brightness 0

# Dim to 20% between midnight and 6am, no idle timeout
./run.sh --night-start 00:00 --night-end 06:00 --night-brightness 20 --idle-off-minutes 0
```

Manual **Off** from the web UI always takes priority over the schedule. Night dim and idle auto-off are temporary — the display returns when the schedule ends or playback resumes.

## Command-line options

Run `./build/spotify-matrix --help` for a short summary. All flags can be passed to `./run.sh` or `./run-quality.sh` after the script name.

### Matrix hardware

| Flag | Default | Description |
|---|---|---|
| `--rows N` | `64` | Panel rows |
| `--cols N` | `64` | Panel columns |
| `--chain-length N` | `1` | Panel chain length |
| `--parallel N` | `1` | Parallel chains |
| `--brightness N` | `65` | Brightness 1–100 (daytime / normal level) |
| `--gpio-slowdown N` | `2` | GPIO slowdown; use `4` on Pi Zero |
| `--hardware-mapping NAME` | `regular` | `regular`, `adafruit-hat`, or `adafruit-hat-pwm` |
| `--pwm-bits N` | `8` | PWM bits; lower values refresh faster with fewer color steps |
| `--pwm-lsb-nanoseconds N` | `130` | PWM timing; raise if you see ghosting |
| `--pwm-dither-bits N` | `0` | PWM dither bits |
| `--scan-mode N` | `1` | `0` = progressive, `1` = interlaced (less flicker) |
| `--limit-refresh-rate-hz N` | `90` | Cap panel refresh rate |
| `--no-hardware-pulse` | off | Disable hardware pulsing (more flicker, works without audio blacklist) |
| `--no-busy-waiting` | on | Sleep instead of busy-wait when capping refresh |
| `--busy-waiting` | off | Use busy-wait when capping refresh |

### Runtime

| Flag | Default | Description |
|---|---|---|
| `--poll-seconds N` | `3` | How often to poll Spotify |
| `--fps N` | `15` | Display frame rate |
| `--rpm N` | `20` | Vinyl spin speed when playing |
| `--token-cache PATH` | `~/.cache/rgb-spotify/spotify_token.json` | OAuth token cache file |
| `--web-host HOST` | `0.0.0.0` | Web UI bind address |
| `--web-port PORT` | `8080` | Web UI port |
| `--no-web-ui` | off | Disable the mode-switch web UI |

### Schedule

See [Schedule](#schedule) above for behaviour. Flags: `--night-start`, `--night-end`, `--night-brightness`, `--idle-off-minutes`.

### Development and one-shot commands

| Flag | Description |
|---|---|
| `--auth-only` | Authorize Spotify and exit |
| `--no-browser` | Print auth URL without opening a browser |
| `--mock-output PATH` | Write a PNG frame instead of driving matrix hardware |
| `--once` | Render one frame and exit (use with `--mock-output`) |
| `--preview-frames DIR` | Render sample disk frames to a directory and exit |
| `--test-pattern` | Show a moving color-bar test pattern |

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

### Token keeps expiring?

The access token only lasts about an hour; the app should refresh it automatically using the long-lived refresh token stored in the cache file. If you are prompted to re-authorize repeatedly, check the following:

1. **Use the same cache path every time.** `./run.sh` stores tokens at `~/.cache/rgb-spotify/spotify_token.json`. Always authorize through `./run.sh` or pass the same `--token-cache` path explicitly:

```bash
./build/spotify-matrix --auth-only --token-cache ~/.cache/rgb-spotify/spotify_token.json
```

2. **Do not run bare `sudo` without `-E`.** `run.sh` uses `sudo -E` so the token is written to your user home directory, not `/root`. If you previously authorized as root, delete `/root/.cache/rgb-spotify/spotify_token.json` and authorize again via `./run.sh`.

3. **Keep the same Spotify app credentials.** The refresh token is tied to the `SPOTIFY_CLIENT_ID` / `SPOTIFY_CLIENT_SECRET` in `.env`. Changing them invalidates existing tokens.

4. **Check the cache file contains a refresh token:**

```bash
grep refresh_token ~/.cache/rgb-spotify/spotify_token.json
```

If that key is missing, delete the file and run `./build/spotify-matrix --auth-only --token-cache ~/.cache/rgb-spotify/spotify_token.json` once.

On startup the app prints `Spotify token cache: ...` so you can confirm which file it is using.

## Run on Pi Zero + Adafruit bonnet

`run.sh` is the recommended entry point. It passes anti-flicker settings, schedule defaults, and bonnet options automatically:

```bash
./run.sh
```

Equivalent manual command (without schedule defaults):

```bash
sudo -E ./build/spotify-matrix \
  --token-cache ~/.cache/rgb-spotify/spotify_token.json \
  --rows 64 \
  --cols 64 \
  --chain-length 1 \
  --parallel 1 \
  --gpio-slowdown 4 \
  --pwm-bits 8 \
  --scan-mode 1 \
  --limit-refresh-rate-hz 90 \
  --no-busy-waiting \
  --no-hardware-pulse \
  --hardware-mapping adafruit-hat
```

Useful overrides:

```bash
./run.sh --brightness 50 --fps 10
./run.sh --no-web-ui
./run.sh --idle-off-minutes 0
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

- `src/main.cpp` — main loop, schedule logic, Spotify polling thread
- `src/spotify_client.cpp` — OAuth, token cache, currently-playing API
- `src/image_renderer.cpp` — album art download, vinyl record rendering
- `src/now_playing_renderer.cpp` — track info and podcast text layout
- `src/display.cpp` — RGB matrix and mock PNG output
- `src/web_server.cpp` — mode-switch web UI
- `src/schedule.cpp` — night dim and idle auto-off
- `run.sh` / `run-quality.sh` — launch scripts with sensible defaults
- `.env` — local Spotify credentials, ignored by Git
- `.env.example` — template for recreating local config

## Reducing flicker

Biggest improvement: `./run-quality.sh` after the GPIO 4→18 solder mod and disabling onboard audio.

Without the hardware mod, `./run.sh` already applies software anti-flicker settings for a 64×64 Pi Zero panel:

- `--pwm-bits 8` — faster panel refresh (slightly fewer color steps)
- `--scan-mode 1` — interlaced scan, less visible flicker at lower refresh rates
- `--limit-refresh-rate-hz 90` — caps refresh to reduce timing jitter
- `--no-busy-waiting` — avoids starving the Pi Zero CPU while waiting for refresh
- `--no-hardware-pulse` — works without the GPIO 4→18 mod or audio blacklist

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
