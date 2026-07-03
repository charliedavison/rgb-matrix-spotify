#pragma once

#include "image_renderer.hpp"
#include "playback_state.hpp"

const ImageBuffer& render_now_playing(const NowPlayingSnapshot& snapshot, int size, double delta_seconds);
