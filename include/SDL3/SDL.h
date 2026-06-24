#pragma once

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>

using Uint8 = uint8_t;
using Uint16 = uint16_t;
using Uint32 = uint32_t;
using Uint64 = uint64_t;
using Sint8 = int8_t;
using Sint16 = int16_t;
using Sint32 = int32_t;
using Sint64 = int64_t;

struct SDL_Window {};
struct SDL_Renderer {};
struct SDL_Texture {};
struct SDL_Joystick {};
struct SDL_Gamepad {};
using SDL_JoystickID = uint32_t;
using SDL_TouchID = int64_t;
using SDL_FingerID = int64_t;
using SDL_PropertiesID = uint32_t;
using SDL_SensorType = int32_t;

struct SDL_Rect {
  int x;
  int y;
  int w;
  int h;
};

inline const char* SDL_GetError() { return "SDL Switch compatibility stub"; }
inline int SDL_strcmp(const char* lhs, const char* rhs) { return std::strcmp(lhs, rhs); }
inline void SDL_free(void* ptr) { std::free(ptr); }
inline int SDL_AddGamepadMappingsFromFile(const char*) { return 0; }
inline bool SDL_SetAppMetadata(const char*, const char*, const char*) { return true; }

enum SDL_Folder {
  SDL_FOLDER_HOME = 0,
  SDL_FOLDER_DESKTOP,
  SDL_FOLDER_DOCUMENTS,
  SDL_FOLDER_DOWNLOADS,
  SDL_FOLDER_MUSIC,
  SDL_FOLDER_PICTURES,
  SDL_FOLDER_PUBLICSHARE,
  SDL_FOLDER_SAVEDGAMES,
  SDL_FOLDER_SCREENSHOTS,
  SDL_FOLDER_TEMPLATES,
  SDL_FOLDER_VIDEOS,
};
inline const char* SDL_GetUserFolder(SDL_Folder) { return nullptr; }

enum SDL_IOStatus {
  SDL_IO_STATUS_READY,
  SDL_IO_STATUS_ERROR,
  SDL_IO_STATUS_EOF,
};

enum SDL_IOWhence {
  SDL_IO_SEEK_SET = SEEK_SET,
  SDL_IO_SEEK_CUR = SEEK_CUR,
  SDL_IO_SEEK_END = SEEK_END,
};

struct SDL_IOStream {
  std::FILE* file = nullptr;
  SDL_IOStatus status = SDL_IO_STATUS_READY;
};

inline SDL_IOStream* SDL_IOFromFile(const char* path, const char* mode) {
  if (path == nullptr || mode == nullptr) {
    return nullptr;
  }
  auto* file = std::fopen(path, mode);
  if (file == nullptr) {
    return nullptr;
  }
  return new SDL_IOStream{file, SDL_IO_STATUS_READY};
}

inline bool SDL_CloseIO(SDL_IOStream* stream) {
  if (stream == nullptr) {
    return false;
  }
  const bool ok = stream->file == nullptr || std::fclose(stream->file) == 0;
  delete stream;
  return ok;
}

inline size_t SDL_ReadIO(SDL_IOStream* stream, void* ptr, size_t size) {
  if (stream == nullptr || stream->file == nullptr || ptr == nullptr) {
    return 0;
  }
  const size_t read = std::fread(ptr, 1, size, stream->file);
  if (read < size) {
    stream->status = std::feof(stream->file) ? SDL_IO_STATUS_EOF : SDL_IO_STATUS_ERROR;
  }
  return read;
}

inline size_t SDL_WriteIO(SDL_IOStream* stream, const void* ptr, size_t size) {
  if (stream == nullptr || stream->file == nullptr || ptr == nullptr) {
    return 0;
  }
  const size_t written = std::fwrite(ptr, 1, size, stream->file);
  if (written < size) {
    stream->status = SDL_IO_STATUS_ERROR;
  }
  return written;
}

inline bool SDL_FlushIO(SDL_IOStream* stream) {
  return stream != nullptr && stream->file != nullptr && std::fflush(stream->file) == 0;
}

inline Sint64 SDL_SeekIO(SDL_IOStream* stream, Sint64 offset, SDL_IOWhence whence) {
  if (stream == nullptr || stream->file == nullptr) {
    return -1;
  }
  if (std::fseek(stream->file, static_cast<long>(offset), static_cast<int>(whence)) != 0) {
    stream->status = SDL_IO_STATUS_ERROR;
    return -1;
  }
  return static_cast<Sint64>(std::ftell(stream->file));
}

inline Sint64 SDL_TellIO(SDL_IOStream* stream) {
  if (stream == nullptr || stream->file == nullptr) {
    return -1;
  }
  return static_cast<Sint64>(std::ftell(stream->file));
}

inline Sint64 SDL_GetIOSize(SDL_IOStream* stream) {
  if (stream == nullptr || stream->file == nullptr) {
    return -1;
  }
  const long pos = std::ftell(stream->file);
  if (pos < 0 || std::fseek(stream->file, 0, SEEK_END) != 0) {
    return -1;
  }
  const long size = std::ftell(stream->file);
  std::fseek(stream->file, pos, SEEK_SET);
  return size < 0 ? -1 : static_cast<Sint64>(size);
}

inline SDL_IOStatus SDL_GetIOStatus(SDL_IOStream* stream) {
  return stream != nullptr ? stream->status : SDL_IO_STATUS_ERROR;
}

inline bool SDL_ReadU32LE(SDL_IOStream* stream, Uint32* value) {
  Uint8 bytes[4]{};
  if (SDL_ReadIO(stream, bytes, sizeof(bytes)) != sizeof(bytes)) {
    return false;
  }
  *value = static_cast<Uint32>(bytes[0]) | (static_cast<Uint32>(bytes[1]) << 8) |
           (static_cast<Uint32>(bytes[2]) << 16) | (static_cast<Uint32>(bytes[3]) << 24);
  return true;
}

inline bool SDL_WriteU8(SDL_IOStream* stream, Uint8 value) {
  return SDL_WriteIO(stream, &value, sizeof(value)) == sizeof(value);
}

inline bool SDL_WriteU32LE(SDL_IOStream* stream, Uint32 value) {
  const Uint8 bytes[4] = {
      static_cast<Uint8>(value & 0xff),
      static_cast<Uint8>((value >> 8) & 0xff),
      static_cast<Uint8>((value >> 16) & 0xff),
      static_cast<Uint8>((value >> 24) & 0xff),
  };
  return SDL_WriteIO(stream, bytes, sizeof(bytes)) == sizeof(bytes);
}

inline bool SDL_WriteS32LE(SDL_IOStream* stream, Sint32 value) {
  return SDL_WriteU32LE(stream, static_cast<Uint32>(value));
}

inline const char* SDL_GetBasePath() { return "romfs:/"; }

inline char* SDL_GetPrefPath(const char*, const char*) {
  // Single-folder layout (HayatoG/dusklight#5): the prefPath IS the data dir. With no
  // data_location.json descriptor, default_data_path() returns this verbatim, so config, saves,
  // caches and logs all live under one folder. Must match DUSK_SD_DATA_ROOT in
  // platforms/switch/src/switch_stubs.cpp.
  constexpr const char* path = "sdmc:/TwilitRealm/Dusklight/";
  auto* out = static_cast<char*>(std::malloc(std::strlen(path) + 1));
  if (out != nullptr) {
    std::strcpy(out, path);
  }
  return out;
}

enum SDL_PathType {
  SDL_PATHTYPE_NONE,
  SDL_PATHTYPE_FILE,
  SDL_PATHTYPE_DIRECTORY,
};

struct SDL_PathInfo {
  SDL_PathType type = SDL_PATHTYPE_NONE;
  Uint64 size = 0;
};

inline bool SDL_GetPathInfo(const char* path, SDL_PathInfo* info) {
  if (path == nullptr || info == nullptr) {
    return false;
  }
  std::error_code ec;
  const std::filesystem::path fsPath(path);
  if (!std::filesystem::exists(fsPath, ec) || ec) {
    return false;
  }
  info->type = std::filesystem::is_directory(fsPath, ec) ? SDL_PATHTYPE_DIRECTORY : SDL_PATHTYPE_FILE;
  info->size = info->type == SDL_PATHTYPE_FILE ? std::filesystem::file_size(fsPath, ec) : 0;
  return !ec;
}

inline bool SDL_CreateDirectory(const char* path) {
  std::error_code ec;
  return path != nullptr && (std::filesystem::create_directories(path, ec) || std::filesystem::exists(path, ec));
}

inline bool SDL_RemovePath(const char* path) {
  std::error_code ec;
  return path != nullptr && std::filesystem::remove(path, ec) && !ec;
}

enum SDL_EventType : Uint32 {
  SDL_EVENT_QUIT = 0x100,
  SDL_EVENT_WINDOW_MOVED,
  SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED,
  SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED,
  SDL_EVENT_WINDOW_MOUSE_LEAVE,
  SDL_EVENT_WINDOW_FOCUS_LOST,
  SDL_EVENT_WINDOW_FOCUS_GAINED,
  SDL_EVENT_WINDOW_MINIMIZED,
  SDL_EVENT_WINDOW_RESTORED,
  SDL_EVENT_KEY_DOWN,
  SDL_EVENT_KEY_UP,
  SDL_EVENT_TEXT_INPUT,
  SDL_EVENT_MOUSE_MOTION,
  SDL_EVENT_MOUSE_BUTTON_DOWN,
  SDL_EVENT_MOUSE_BUTTON_UP,
  SDL_EVENT_MOUSE_WHEEL,
  SDL_EVENT_FINGER_DOWN,
  SDL_EVENT_FINGER_UP,
  SDL_EVENT_FINGER_MOTION,
  SDL_EVENT_FINGER_CANCELED,
  SDL_EVENT_GAMEPAD_ADDED,
  SDL_EVENT_GAMEPAD_REMOVED,
  SDL_EVENT_GAMEPAD_REMAPPED,
  SDL_EVENT_GAMEPAD_BUTTON_DOWN,
  SDL_EVENT_GAMEPAD_BUTTON_UP,
  SDL_EVENT_GAMEPAD_AXIS_MOTION,
};

struct SDL_GamepadDeviceEvent {
  Uint32 type;
  SDL_JoystickID which;
};

struct SDL_GamepadButtonEvent {
  Uint32 type;
  SDL_JoystickID which;
  Uint8 button;
  bool down;
};

struct SDL_GamepadAxisEvent {
  Uint32 type;
  SDL_JoystickID which;
  Uint8 axis;
  Sint16 value;
};

struct SDL_TouchFingerEvent {
  Uint32 type;
  SDL_TouchID touchID;
  SDL_FingerID fingerID;
  float x;
  float y;
  float dx;
  float dy;
  float pressure;
};

struct SDL_MouseMotionEvent {
  Uint32 type;
  float x;
  float y;
};

struct SDL_MouseButtonEvent {
  Uint32 type;
  Uint8 button;
  float x;
  float y;
};

struct SDL_MouseWheelEvent {
  Uint32 type;
  float x;
  float y;
};

union SDL_Event {
  Uint32 type;
  SDL_GamepadDeviceEvent gdevice;
  SDL_GamepadButtonEvent gbutton;
  SDL_GamepadAxisEvent gaxis;
  SDL_TouchFingerEvent tfinger;
  SDL_MouseMotionEvent motion;
  SDL_MouseButtonEvent button;
  SDL_MouseWheelEvent wheel;
};

// Custom (user) event range starts at SDL_EVENT_USER in real SDL3. aurora registers a small range
// for its own internal events; we hand out a monotonically increasing base (always non-zero so the
// caller's success ASSERT passes).
constexpr Uint32 SDL_EVENT_USER = 0x8000;
inline Uint32 SDL_RegisterEvents(int numevents) {
  static Uint32 next = SDL_EVENT_USER;
  const Uint32 base = next;
  next += static_cast<Uint32>(numevents > 0 ? numevents : 0);
  return base;
}

enum SDL_GamepadButton {
  SDL_GAMEPAD_BUTTON_INVALID = -1,
  SDL_GAMEPAD_BUTTON_SOUTH,
  SDL_GAMEPAD_BUTTON_EAST,
  SDL_GAMEPAD_BUTTON_WEST,
  SDL_GAMEPAD_BUTTON_NORTH,
  SDL_GAMEPAD_BUTTON_BACK,
  SDL_GAMEPAD_BUTTON_GUIDE,
  SDL_GAMEPAD_BUTTON_START,
  SDL_GAMEPAD_BUTTON_LEFT_STICK,
  SDL_GAMEPAD_BUTTON_RIGHT_STICK,
  SDL_GAMEPAD_BUTTON_LEFT_SHOULDER,
  SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER,
  SDL_GAMEPAD_BUTTON_DPAD_UP,
  SDL_GAMEPAD_BUTTON_DPAD_DOWN,
  SDL_GAMEPAD_BUTTON_DPAD_LEFT,
  SDL_GAMEPAD_BUTTON_DPAD_RIGHT,
  SDL_GAMEPAD_BUTTON_MISC1,
  SDL_GAMEPAD_BUTTON_RIGHT_PADDLE1,
  SDL_GAMEPAD_BUTTON_LEFT_PADDLE1,
  SDL_GAMEPAD_BUTTON_RIGHT_PADDLE2,
  SDL_GAMEPAD_BUTTON_LEFT_PADDLE2,
  SDL_GAMEPAD_BUTTON_TOUCHPAD,
  SDL_GAMEPAD_BUTTON_MISC2,
  SDL_GAMEPAD_BUTTON_MISC3,
  SDL_GAMEPAD_BUTTON_MISC4,
  SDL_GAMEPAD_BUTTON_MISC5,
  SDL_GAMEPAD_BUTTON_MISC6,
  SDL_GAMEPAD_BUTTON_COUNT,
};

enum SDL_GamepadAxis {
  SDL_GAMEPAD_AXIS_INVALID = -1,
  SDL_GAMEPAD_AXIS_LEFTX,
  SDL_GAMEPAD_AXIS_LEFTY,
  SDL_GAMEPAD_AXIS_RIGHTX,
  SDL_GAMEPAD_AXIS_RIGHTY,
  SDL_GAMEPAD_AXIS_LEFT_TRIGGER,
  SDL_GAMEPAD_AXIS_RIGHT_TRIGGER,
  SDL_GAMEPAD_AXIS_COUNT,
};

enum SDL_GamepadButtonLabel {
  SDL_GAMEPAD_BUTTON_LABEL_UNKNOWN,
  SDL_GAMEPAD_BUTTON_LABEL_A,
  SDL_GAMEPAD_BUTTON_LABEL_B,
  SDL_GAMEPAD_BUTTON_LABEL_X,
  SDL_GAMEPAD_BUTTON_LABEL_Y,
  SDL_GAMEPAD_BUTTON_LABEL_CROSS,
  SDL_GAMEPAD_BUTTON_LABEL_CIRCLE,
  SDL_GAMEPAD_BUTTON_LABEL_SQUARE,
  SDL_GAMEPAD_BUTTON_LABEL_TRIANGLE,
};

enum SDL_GamepadType {
  SDL_GAMEPAD_TYPE_UNKNOWN,
  SDL_GAMEPAD_TYPE_STANDARD,
  SDL_GAMEPAD_TYPE_XBOX360,
  SDL_GAMEPAD_TYPE_XBOXONE,
  SDL_GAMEPAD_TYPE_PS3,
  SDL_GAMEPAD_TYPE_PS4,
  SDL_GAMEPAD_TYPE_PS5,
  SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_PRO,
  SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_LEFT,
  SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_RIGHT,
  SDL_GAMEPAD_TYPE_GAMECUBE,
};

enum SDL_JoystickConnectionState {
  SDL_JOYSTICK_CONNECTION_INVALID = -1,
  SDL_JOYSTICK_CONNECTION_UNKNOWN,
  SDL_JOYSTICK_CONNECTION_WIRED,
  SDL_JOYSTICK_CONNECTION_WIRELESS,
};

enum SDL_PowerState {
  SDL_POWERSTATE_ERROR = -1,
  SDL_POWERSTATE_UNKNOWN,
  SDL_POWERSTATE_ON_BATTERY,
  SDL_POWERSTATE_NO_BATTERY,
  SDL_POWERSTATE_CHARGING,
  SDL_POWERSTATE_CHARGED,
};

inline SDL_Gamepad* SDL_GetGamepadFromID(SDL_JoystickID) { return nullptr; }
inline bool SDL_GamepadConnected(SDL_Gamepad*) { return false; }
inline const char* SDL_GetGamepadName(SDL_Gamepad*) { return "Switch Controller"; }
inline const char* SDL_GetGamepadNameForID(SDL_JoystickID) { return "Switch Controller"; }
inline SDL_JoystickConnectionState SDL_GetGamepadConnectionState(SDL_Gamepad*) {
  return SDL_JOYSTICK_CONNECTION_WIRELESS;
}
inline SDL_PowerState SDL_GetGamepadPowerInfo(SDL_Gamepad*, int* level) {
  if (level != nullptr) {
    *level = -1;
  }
  return SDL_POWERSTATE_UNKNOWN;
}
inline SDL_Joystick* SDL_GetGamepadJoystick(SDL_Gamepad*) { return nullptr; }
inline SDL_JoystickID SDL_GetJoystickID(SDL_Joystick*) { return 0; }
inline SDL_PowerState SDL_GetJoystickPowerInfo(SDL_Joystick*, int* level) {
  if (level != nullptr) {
    *level = -1;
  }
  return SDL_POWERSTATE_UNKNOWN;
}
inline SDL_GamepadType SDL_GetGamepadType(SDL_Gamepad*) { return SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_PRO; }
inline Uint8 SDL_GetGamepadButton(SDL_Gamepad*, SDL_GamepadButton) { return 0; }
inline Sint16 SDL_GetGamepadAxis(SDL_Gamepad*, SDL_GamepadAxis) { return 0; }
inline SDL_GamepadButtonLabel SDL_GetGamepadButtonLabel(SDL_Gamepad*, SDL_GamepadButton) {
  return SDL_GAMEPAD_BUTTON_LABEL_UNKNOWN;
}
inline int SDL_GetGamepadPlayerIndex(SDL_Gamepad*) { return -1; }
inline bool SDL_SetGamepadPlayerIndex(SDL_Gamepad*, int) { return true; }
inline SDL_PropertiesID SDL_GetGamepadProperties(SDL_Gamepad*) { return 0; }
inline Uint16 SDL_GetGamepadVendor(SDL_Gamepad*) { return 0; }
inline Uint16 SDL_GetGamepadProduct(SDL_Gamepad*) { return 0; }
inline SDL_JoystickID SDL_GetGamepadID(SDL_Gamepad*) { return 0; }
inline const char* SDL_GetGamepadSerial(SDL_Gamepad*) { return nullptr; }
inline bool SDL_RumbleGamepad(SDL_Gamepad*, Uint16, Uint16, Uint32) { return false; }
inline bool SDL_SetGamepadLED(SDL_Gamepad*, Uint8, Uint8, Uint8) { return false; }
inline void SDL_CloseGamepad(SDL_Gamepad*) {}

constexpr Sint16 SDL_JOYSTICK_AXIS_MAX = 32767;

inline Uint64 SDL_GetTicksNS() {
  static const auto start = std::chrono::steady_clock::now();
  return static_cast<Uint64>(
      std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - start).count());
}
inline Uint64 SDL_GetPerformanceCounter() { return SDL_GetTicksNS(); }
inline Uint64 SDL_GetPerformanceFrequency() { return 1000000000ull; }
inline void SDL_DelayPrecise(Uint64) {}

enum SDL_Scancode {
  SDL_SCANCODE_UNKNOWN = 0,
  SDL_SCANCODE_ESCAPE = 41,
};

inline const bool* SDL_GetKeyboardState(int* count) {
  static bool keys[512]{};
  if (count != nullptr) {
    *count = static_cast<int>(std::size(keys));
  }
  return keys;
}
inline const char* SDL_GetScancodeName(SDL_Scancode) { return ""; }

enum SDL_TextInputType {
  SDL_TEXTINPUT_TYPE_TEXT,
  SDL_TEXTINPUT_TYPE_NUMBER,
};
inline SDL_PropertiesID SDL_CreateProperties() { return 1; }
inline void SDL_DestroyProperties(SDL_PropertiesID) {}
inline bool SDL_SetNumberProperty(SDL_PropertiesID, const char*, Sint64) { return true; }
inline bool SDL_StartTextInput(SDL_Window*) { return false; }
inline bool SDL_StartTextInputWithProperties(SDL_Window*, SDL_PropertiesID) { return false; }
inline bool SDL_StopTextInput(SDL_Window*) { return true; }
inline bool SDL_TextInputActive(SDL_Window*) { return false; }
inline bool SDL_SetTextInputArea(SDL_Window*, const SDL_Rect*, int) { return true; }

#define SDL_PROP_TEXTINPUT_TYPE_NUMBER "SDL.textinput.type"

constexpr Uint32 SDL_BUTTON_LEFT = 1u << 0;
constexpr Uint32 SDL_BUTTON_MIDDLE = 1u << 1;
constexpr Uint32 SDL_BUTTON_RIGHT = 1u << 2;
constexpr Uint32 SDL_BUTTON_X1 = 1u << 3;
constexpr Uint32 SDL_BUTTON_X2 = 1u << 4;

inline Uint32 SDL_GetMouseState(float* x, float* y) {
  if (x != nullptr) {
    *x = 0.f;
  }
  if (y != nullptr) {
    *y = 0.f;
  }
  return 0;
}
inline Uint32 SDL_GetGlobalMouseState(float* x, float* y) { return SDL_GetMouseState(x, y); }
inline Uint32 SDL_GetRelativeMouseState(float* x, float* y) { return SDL_GetMouseState(x, y); }
inline bool SDL_CaptureMouse(bool) { return true; }
inline bool SDL_ShowCursor() { return true; }
inline bool SDL_HideCursor() { return true; }

// Relative-mouse / grab / warp are no-ops on Switch (no desktop mouse). The Switch window never
// reports SDL_WINDOW_INPUT_FOCUS (SDL_GetWindowFlags returns 0), so dusk's mouse-capture path stays
// inert — these exist only so dusk/mouse.cpp compiles.
constexpr Uint32 SDL_WINDOW_INPUT_FOCUS = 0x200u;
inline bool SDL_GetWindowRelativeMouseMode(SDL_Window*) { return false; }
inline bool SDL_SetWindowRelativeMouseMode(SDL_Window*, bool) { return true; }
inline bool SDL_SetWindowMouseGrab(SDL_Window*, bool) { return true; }
inline void SDL_WarpMouseInWindow(SDL_Window*, float, float) {}

inline bool SDL_GetWindowSafeArea(SDL_Window*, SDL_Rect* rect) {
  if (rect != nullptr) {
    *rect = SDL_Rect{0, 0, 1280, 720};
  }
  return true;
}
inline Uint32 SDL_GetWindowFlags(SDL_Window*) { return 0; }
inline bool SDL_GetWindowSize(SDL_Window*, int* w, int* h) {
  if (w != nullptr) {
    *w = 1280;
  }
  if (h != nullptr) {
    *h = 720;
  }
  return true;
}
inline bool SDL_GetWindowSizeInPixels(SDL_Window* window, int* w, int* h) {
  return SDL_GetWindowSize(window, w, h);
}
inline float SDL_GetWindowDisplayScale(SDL_Window*) { return 1.f; }

struct SDL_DialogFileFilter {
  const char* name;
  const char* pattern;
};

using SDL_DialogFileCallback = void (*)(void*, const char* const*, int);
inline void SDL_ShowOpenFileDialog(SDL_DialogFileCallback callback, void* userdata, SDL_Window*,
                                   const SDL_DialogFileFilter*, int, const char*, bool) {
  if (callback != nullptr) {
    static const char* empty[] = {nullptr};
    callback(userdata, empty, 0);
  }
}

inline bool SDL_OpenURL(const char*) { return false; }

// ── Audio ──────────────────────────────────────────────────────────────────
// dusk/audio/DuskAudioSystem.cpp drives audio through these. On Switch they are NOT inline — they
// are implemented over libnx audren in platforms/switch/src/switch_audio.cpp (the reference build
// uses SDL3's own audren backend; we mirror just the surface DuskAudioSystem needs).
#ifndef SDLCALL
#define SDLCALL
#endif
constexpr Uint32 SDL_INIT_AUDIO = 0x00000010u;
bool SDL_Init(Uint32 flags);

using SDL_AudioFormat = Uint32;
constexpr SDL_AudioFormat SDL_AUDIO_S16 = 0x8010u;
constexpr SDL_AudioFormat SDL_AUDIO_S32 = 0x8020u;
constexpr SDL_AudioFormat SDL_AUDIO_F32 = 0x8120u; // float32, native-endian

struct SDL_AudioSpec {
  SDL_AudioFormat format;
  int channels;
  int freq;
};

struct SDL_AudioStream; // opaque; defined in switch_audio.cpp
using SDL_AudioDeviceID = Uint32;
constexpr SDL_AudioDeviceID SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK = 0xFFFFFFFFu;

using SDL_AudioStreamCallback = void(SDLCALL*)(void* userdata, SDL_AudioStream* stream,
                                               int additional_amount, int total_amount);

SDL_AudioStream* SDL_OpenAudioDeviceStream(SDL_AudioDeviceID devid, const SDL_AudioSpec* spec,
                                           SDL_AudioStreamCallback callback, void* userdata);
bool SDL_PutAudioStreamData(SDL_AudioStream* stream, const void* buf, int len);
bool SDL_ResumeAudioStreamDevice(SDL_AudioStream* stream);
bool SDL_PauseAudioStreamDevice(SDL_AudioStream* stream);

struct SDL_Surface {
  int w = 0;
  int h = 0;
  int pitch = 0;
  void* pixels = nullptr;
};
constexpr Uint32 SDL_PIXELFORMAT_RGBA32 = 0;

enum SDL_ScaleMode {
  SDL_SCALEMODE_INVALID = -1,
  SDL_SCALEMODE_NEAREST,
  SDL_SCALEMODE_LINEAR,
};

using SDL_BlendMode = Uint32;
constexpr SDL_BlendMode SDL_BLENDMODE_NONE = 0x00000000u;
constexpr SDL_BlendMode SDL_BLENDMODE_BLEND = 0x00000001u;

inline SDL_Surface* SDL_LoadPNG_IO(SDL_IOStream*, bool closeio) {
  (void)closeio;
  return nullptr;
}
inline SDL_Surface* SDL_ConvertSurface(SDL_Surface* surface, Uint32) { return surface; }

// On Switch (no SDL) we back surfaces with a heap RGBA32 pixel buffer so dusk/ui/icon_provider.cpp
// can compose icons. Only icon_provider creates/destroys surfaces on Switch (window.cpp isn't
// built; the rmlui PNG path early-returns on the null SDL_LoadPNG_IO), so the create/destroy pair
// can't double-free.
inline SDL_Surface* SDL_CreateSurface(int w, int h, Uint32 /*format*/) {
  if (w <= 0 || h <= 0) {
    return nullptr;
  }
  auto* s = new SDL_Surface{};
  s->w = w;
  s->h = h;
  s->pitch = w * 4; // RGBA32
  s->pixels = std::calloc(static_cast<size_t>(w) * static_cast<size_t>(h), 4);
  if (s->pixels == nullptr) {
    delete s;
    return nullptr;
  }
  return s;
}
inline void SDL_DestroySurface(SDL_Surface* surface) {
  if (surface != nullptr) {
    std::free(surface->pixels);
    delete surface;
  }
}
inline bool SDL_MUSTLOCK(SDL_Surface*) { return false; }
inline bool SDL_LockSurface(SDL_Surface*) { return true; }
inline void SDL_UnlockSurface(SDL_Surface*) {}
inline bool SDL_SetSurfaceBlendMode(SDL_Surface*, SDL_BlendMode) { return true; }

// Nearest-neighbour scaled blit with src-over alpha compositing (RGBA32 only). Enough for the
// layered icon composition in icon_provider; scaleMode is ignored (always nearest).
inline bool SDL_BlitSurfaceScaled(SDL_Surface* src, const SDL_Rect* srcrect, SDL_Surface* dst,
                                  const SDL_Rect* dstrect, SDL_ScaleMode) {
  if (src == nullptr || dst == nullptr || src->pixels == nullptr || dst->pixels == nullptr) {
    return false;
  }
  const int sx = srcrect != nullptr ? srcrect->x : 0;
  const int sy = srcrect != nullptr ? srcrect->y : 0;
  const int sw = srcrect != nullptr ? srcrect->w : src->w;
  const int sh = srcrect != nullptr ? srcrect->h : src->h;
  const int dx = dstrect != nullptr ? dstrect->x : 0;
  const int dy = dstrect != nullptr ? dstrect->y : 0;
  const int dw = dstrect != nullptr ? dstrect->w : dst->w;
  const int dh = dstrect != nullptr ? dstrect->h : dst->h;
  if (sw <= 0 || sh <= 0 || dw <= 0 || dh <= 0) {
    return false;
  }
  const auto* srcPixels = static_cast<const Uint8*>(src->pixels);
  auto* dstPixels = static_cast<Uint8*>(dst->pixels);
  for (int j = 0; j < dh; ++j) {
    const int dyPix = dy + j;
    if (dyPix < 0 || dyPix >= dst->h) {
      continue;
    }
    const int syPix = sy + (j * sh) / dh;
    if (syPix < 0 || syPix >= src->h) {
      continue;
    }
    for (int i = 0; i < dw; ++i) {
      const int dxPix = dx + i;
      if (dxPix < 0 || dxPix >= dst->w) {
        continue;
      }
      const int sxPix = sx + (i * sw) / dw;
      if (sxPix < 0 || sxPix >= src->w) {
        continue;
      }
      const Uint8* sp = srcPixels + static_cast<size_t>(syPix) * src->pitch + static_cast<size_t>(sxPix) * 4;
      Uint8* dp = dstPixels + static_cast<size_t>(dyPix) * dst->pitch + static_cast<size_t>(dxPix) * 4;
      const Uint8 sa = sp[3];
      if (sa == 0) {
        continue;
      }
      if (sa == 255) {
        std::memcpy(dp, sp, 4);
      } else {
        const int ia = 255 - sa;
        dp[0] = static_cast<Uint8>((sp[0] * sa + dp[0] * ia) / 255);
        dp[1] = static_cast<Uint8>((sp[1] * sa + dp[1] * ia) / 255);
        dp[2] = static_cast<Uint8>((sp[2] * sa + dp[2] * ia) / 255);
        dp[3] = static_cast<Uint8>(sa + (dp[3] * ia) / 255);
      }
    }
  }
  return true;
}
