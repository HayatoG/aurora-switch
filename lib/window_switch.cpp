#include "window.hpp"

#ifdef AURORA_ENABLE_GX
#include "webgpu/gpu.hpp"
#endif
#include "input.hpp"
#include "internal.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <utility>
#include <vector>

#include <switch.h>
#include <cstdio>

extern "C" void dusk_switch_log(const char*);

namespace aurora::window {
namespace {
Module Log("aurora::window");

bool g_windowCreated = false;
float g_frameBufferScale = 0.f;
bool g_frameBufferAspectFit = false;
AuroraWindowSize g_windowSize{};
std::vector<AuroraEvent> g_events;
std::atomic_bool g_surfaceReady = true;
bool g_exitRequested = false;
PadState g_pad{};

constexpr u32 kSwitchSurfaceWidth = 1280;
constexpr u32 kSwitchSurfaceHeight = 720;

bool operator==(const AuroraWindowSize& lhs, const AuroraWindowSize& rhs) {
  return lhs.width == rhs.width && lhs.height == rhs.height && lhs.fb_width == rhs.fb_width &&
         lhs.fb_height == rhs.fb_height && lhs.native_fb_height == rhs.native_fb_height &&
         lhs.native_fb_width == rhs.native_fb_width && lhs.scale == rhs.scale;
}

std::pair<int, int> scale_frame_buffer_to_aspect(int w, int h, float scale, float aspect) {
  if (w <= 0 || h <= 0 || scale <= 0.f || aspect <= 0.f) {
    return {std::max(w, 1), std::max(h, 1)};
  }
  const int baseW = std::max(1, static_cast<int>(std::lround(static_cast<float>(w) * scale)));
  const int baseH = std::max(1, static_cast<int>(std::lround(static_cast<float>(h) * scale)));
  if (aspect >= static_cast<float>(w) / static_cast<float>(h)) {
    return {std::max(1, static_cast<int>(std::lround(static_cast<float>(baseH) * aspect))), baseH};
  }
  return {baseW, std::max(1, static_cast<int>(std::lround(static_cast<float>(baseW) / aspect)))};
}

std::pair<int, int> fit_frame_buffer_to_aspect(int width, int height, float aspect) {
  if (width <= 0 || height <= 0 || aspect <= 0.f) {
    return {std::max(width, 1), std::max(height, 1)};
  }
  if (static_cast<float>(width) / static_cast<float>(height) > aspect) {
    return {std::max(1, static_cast<int>(std::lround(static_cast<float>(height) * aspect))), height};
  }
  return {width, std::max(1, static_cast<int>(std::lround(static_cast<float>(width) / aspect)))};
}

void resize_swapchain() noexcept {
  const auto size = get_window_size();
  if (size == g_windowSize) {
    return;
  }
  g_windowSize = size;
#ifdef AURORA_ENABLE_GX
  webgpu::resize_swapchain(size.fb_width, size.fb_height, size.native_fb_width, size.native_fb_height);
#endif
}
} // namespace

SurfaceLock::SurfaceLock() noexcept = default;
SurfaceLock::~SurfaceLock() = default;

bool initialize() {
  padConfigureInput(1, HidNpadStyleSet_NpadStandard);
  padInitializeDefault(&g_pad);
  return nwindowGetDefault() != nullptr;
}

bool initialize_event_watch() { return true; }

void shutdown() { destroy_window(); }

bool create_window(AuroraBackend) {
  auto* window = nwindowGetDefault();
  if (window == nullptr) {
    Log.error("nwindowGetDefault returned null");
    return false;
  }

  const u32 width = kSwitchSurfaceWidth;
  const u32 height = kSwitchSurfaceHeight;
  Result rc = nwindowSetDimensions(window, width, height);
  if (R_FAILED(rc)) {
    Log.warn("nwindowSetDimensions failed: 0x{:x}", static_cast<unsigned>(rc));
  }
  rc = nwindowSetCrop(window, 0, 0, static_cast<s32>(width), static_cast<s32>(height));
  if (R_FAILED(rc)) {
    Log.warn("nwindowSetCrop failed: 0x{:x}", static_cast<unsigned>(rc));
  }
  rc = nwindowSetSwapInterval(window, 1);
  if (R_FAILED(rc)) {
    Log.warn("nwindowSetSwapInterval failed: 0x{:x}", static_cast<unsigned>(rc));
  }

  g_windowCreated = true;
  g_surfaceReady.store(true, std::memory_order_release);
  g_windowSize = get_window_size();
  return true;
}

bool create_renderer() { return false; }

void destroy_window() {
  g_windowCreated = false;
  g_surfaceReady.store(false, std::memory_order_release);
}

void show_window() {}

AuroraWindowSize get_window_size() {
  u32 nativeW = kSwitchSurfaceWidth;
  u32 nativeH = kSwitchSurfaceHeight;
  if (auto* window = nwindowGetDefault()) {
    const Result rc = nwindowGetDimensions(window, &nativeW, &nativeH);
    if (R_FAILED(rc) || nativeW == 0 || nativeH == 0) {
      nativeW = kSwitchSurfaceWidth;
      nativeH = kSwitchSurfaceHeight;
    }
  }

  int fbW = static_cast<int>(nativeW);
  int fbH = static_cast<int>(nativeH);
  if (g_frameBufferScale > 0.f) {
    const auto [scaledW, scaledH] =
        scale_frame_buffer_to_aspect(fbW, fbH, g_frameBufferScale, static_cast<float>(fbW) / static_cast<float>(fbH));
    fbW = scaledW;
    fbH = scaledH;
  }
  if (g_frameBufferAspectFit) {
    const uint32_t baseW = g_config.windowWidth;
    const uint32_t baseH = g_config.windowHeight;
    if (baseW > 0 && baseH > 0) {
      const auto [fitW, fitH] =
          fit_frame_buffer_to_aspect(fbW, fbH, static_cast<float>(baseW) / static_cast<float>(baseH));
      fbW = fitW;
      fbH = fitH;
    }
  }

  return {
      .width = nativeW,
      .height = nativeH,
      .fb_width = static_cast<uint32_t>(fbW),
      .fb_height = static_cast<uint32_t>(fbH),
      .native_fb_width = nativeW,
      .native_fb_height = nativeH,
      .scale = 1.f,
  };
}

const AuroraEvent* poll_events() {
  g_events.clear();
  input::set_mouse_scroll(0, 0);

  static unsigned poll_count = 0;
  static bool exit_logged = false;
  bool was_exit = g_exitRequested;
  if (!g_exitRequested && !appletMainLoop()) {
    g_exitRequested = true;
  }
  if (poll_count < 8) {
    char b[128];
    std::snprintf(b, sizeof b, "[poll] iter=%u exitReq=%d->%d\n",
                  poll_count, (int)was_exit, (int)g_exitRequested);
    ::dusk_switch_log(b);
  } else if (g_exitRequested && !exit_logged) {
    char b[128];
    std::snprintf(b, sizeof b, "[poll] late exit iter=%u\n", poll_count);
    ::dusk_switch_log(b);
    exit_logged = true;
  }
  ++poll_count;

  padUpdate(&g_pad);

  // Translate libnx button transitions (down/up this frame) into SDL gamepad
  // events so the Dusk launcher RmlUi navigation receives input. The in-game
  // PADRead() pipeline reads directly from libnx state and is unaffected.
  {
    const u64 down = padGetButtonsDown(&g_pad);
    const u64 up   = padGetButtonsUp(&g_pad);
    struct ButMap { u64 nx; Uint8 sdl; };
    static constexpr ButMap kMap[] = {
        // Nintendo: A=confirm (right), B=cancel (bottom). Map to SDL semantic
        // equivalents — Dusk's input.cpp treats SOUTH as KI_RETURN (confirm)
        // and EAST as KI_ESCAPE (cancel) regardless of physical position.
        { HidNpadButton_A,        SDL_GAMEPAD_BUTTON_SOUTH }, // Switch A → confirm
        { HidNpadButton_B,        SDL_GAMEPAD_BUTTON_EAST  }, // Switch B → cancel
        { HidNpadButton_X,        SDL_GAMEPAD_BUTTON_NORTH },
        { HidNpadButton_Y,        SDL_GAMEPAD_BUTTON_WEST  },
        { HidNpadButton_Plus,     SDL_GAMEPAD_BUTTON_START },
        { HidNpadButton_Minus,    SDL_GAMEPAD_BUTTON_BACK  },
        { HidNpadButton_L,        SDL_GAMEPAD_BUTTON_LEFT_SHOULDER },
        { HidNpadButton_R,        SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER },
        { HidNpadButton_StickL,   SDL_GAMEPAD_BUTTON_LEFT_STICK },
        { HidNpadButton_StickR,   SDL_GAMEPAD_BUTTON_RIGHT_STICK },
        { HidNpadButton_Up,       SDL_GAMEPAD_BUTTON_DPAD_UP },
        { HidNpadButton_Down,     SDL_GAMEPAD_BUTTON_DPAD_DOWN },
        { HidNpadButton_Left,     SDL_GAMEPAD_BUTTON_DPAD_LEFT },
        { HidNpadButton_Right,    SDL_GAMEPAD_BUTTON_DPAD_RIGHT },
    };
    for (const auto& m : kMap) {
      if (down & m.nx) {
        AuroraEvent ev{};
        ev.type = AURORA_SDL_EVENT;
        ev.sdl.gbutton.type = SDL_EVENT_GAMEPAD_BUTTON_DOWN;
        ev.sdl.gbutton.which = 0;
        ev.sdl.gbutton.button = m.sdl;
        ev.sdl.gbutton.down = true;
        g_events.push_back(ev);
      }
      if (up & m.nx) {
        AuroraEvent ev{};
        ev.type = AURORA_SDL_EVENT;
        ev.sdl.gbutton.type = SDL_EVENT_GAMEPAD_BUTTON_UP;
        ev.sdl.gbutton.which = 0;
        ev.sdl.gbutton.button = m.sdl;
        ev.sdl.gbutton.down = false;
        g_events.push_back(ev);
      }
    }
  }

  const auto size = get_window_size();
  if (size != g_windowSize) {
    g_windowSize = size;
    g_events.push_back(AuroraEvent{
        .type = AURORA_WINDOW_RESIZED,
        .windowSize = size,
    });
#ifdef AURORA_ENABLE_GX
    webgpu::resize_swapchain(size.fb_width, size.fb_height, size.native_fb_width, size.native_fb_height);
#endif
  }

  if (g_exitRequested) {
    g_events.push_back(AuroraEvent{.type = AURORA_EXIT});
  }
  g_events.push_back(AuroraEvent{.type = AURORA_NONE});
  return g_events.data();
}

SDL_Window* get_sdl_window() { return g_windowCreated ? reinterpret_cast<SDL_Window*>(nwindowGetDefault()) : nullptr; }

SDL_Renderer* get_sdl_renderer() { return nullptr; }

void* get_native_window() { return nwindowGetDefault(); }

bool is_paused() noexcept { return false; }

bool is_presentable() noexcept { return g_windowCreated && g_surfaceReady.load(std::memory_order_acquire); }

void set_surface_ready(bool ready) noexcept { g_surfaceReady.store(ready, std::memory_order_release); }

bool push_custom_event(CustomEvent eventType) {
  if (eventType == CustomEvent::FutureResize) {
    resize_swapchain();
    return true;
  }
  if (eventType == CustomEvent::RefreshSurface) {
#ifdef AURORA_ENABLE_GX
    SurfaceLock surfaceLock;
    webgpu::refresh_surface(false);
#endif
    return true;
  }
  return true;
}

void set_title(const char*) {}
void set_fullscreen(bool) {}
bool get_fullscreen() { return true; }

void set_window_size(uint32_t, uint32_t) {
  auto* window = nwindowGetDefault();
  if (window == nullptr) {
    return;
  }
  const u32 width = kSwitchSurfaceWidth;
  const u32 height = kSwitchSurfaceHeight;
  Result rc = nwindowSetDimensions(window, width, height);
  if (R_FAILED(rc)) {
    Log.warn("nwindowSetDimensions failed: 0x{:x}", static_cast<unsigned>(rc));
  }
  rc = nwindowSetCrop(window, 0, 0, static_cast<s32>(width), static_cast<s32>(height));
  if (R_FAILED(rc)) {
    Log.warn("nwindowSetCrop failed: 0x{:x}", static_cast<unsigned>(rc));
  }
  request_frame_buffer_resize();
}

void set_window_position(uint32_t, uint32_t) {}
void center_window() {}

void request_frame_buffer_resize() {
  if (g_windowCreated) {
    resize_swapchain();
  }
}

void set_frame_buffer_scale(float scale) {
  if (scale < 0.f) {
    scale = 0.f;
  }
  if (g_frameBufferScale == scale) {
    return;
  }
  g_frameBufferScale = scale;
  request_frame_buffer_resize();
}

void set_frame_buffer_aspect_fit(bool fit) {
  if (g_frameBufferAspectFit == fit) {
    return;
  }
  g_frameBufferAspectFit = fit;
  request_frame_buffer_resize();
}

void set_background_input(bool) {}
} // namespace aurora::window
