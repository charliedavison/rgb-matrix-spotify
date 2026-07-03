cmake_minimum_required(VERSION 3.16)
project(spotify-matrix VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(CURL REQUIRED)
find_package(Threads REQUIRED)

set(RGBMATRIX_ROOT "" CACHE PATH "Path to hzeller/rpi-rgb-led-matrix checkout")
if(NOT RGBMATRIX_ROOT)
  set(RGBMATRIX_ROOT "${CMAKE_SOURCE_DIR}/external/rpi-rgb-led-matrix")
endif()

option(SPOTIFY_MATRIX_MOCK "Build without RGB matrix hardware (mock PNG output only)" OFF)

add_executable(spotify-matrix
  src/main.cpp
  src/config.cpp
  src/util.cpp
  src/http_client.cpp
  src/oauth_server.cpp
  src/spotify_client.cpp
  src/image_renderer.cpp
  src/now_playing_renderer.cpp
  src/playback_state.cpp
  src/schedule.cpp
  src/display_mode.cpp
  src/web_server.cpp
  src/display.cpp
  src/stb_impl.cpp
)

target_include_directories(spotify-matrix PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_SOURCE_DIR}/third_party
)

target_link_libraries(spotify-matrix PRIVATE CURL::libcurl Threads::Threads)

if(SPOTIFY_MATRIX_MOCK)
  target_compile_definitions(spotify-matrix PRIVATE SPOTIFY_MATRIX_MOCK=1)
else()
  if(NOT EXISTS "${RGBMATRIX_ROOT}/include/led-matrix.h")
    message(FATAL_ERROR
      "rpi-rgb-led-matrix not found at ${RGBMATRIX_ROOT}. "
      "Clone https://github.com/hzeller/rpi-rgb-led-matrix or set -DRGBMATRIX_ROOT=...")
  endif()

  set(RGBMATRIX_LIB "${RGBMATRIX_ROOT}/lib/librgbmatrix.a")
  if(NOT EXISTS "${RGBMATRIX_LIB}")
    message(FATAL_ERROR
      "${RGBMATRIX_LIB} not found. Build the matrix library first:\n"
      "  make -C ${RGBMATRIX_ROOT}")
  endif()

  target_include_directories(spotify-matrix PRIVATE "${RGBMATRIX_ROOT}/include")
  target_link_libraries(spotify-matrix PRIVATE
    "${RGBMATRIX_LIB}"
    rt
    m
    pthread
  )
endif()

install(TARGETS spotify-matrix RUNTIME DESTINATION bin)
