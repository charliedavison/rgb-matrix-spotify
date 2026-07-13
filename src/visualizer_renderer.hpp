#pragma once

#include "image_renderer.hpp"
#include "playback_state.hpp"

const ImageBuffer& render_visualizer(const NowPlayingSnapshot& snapshot, int size, double delta_seconds);
