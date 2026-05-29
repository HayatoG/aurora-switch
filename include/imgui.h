#pragma once

// Minimal ImGui stub for the Switch build (AURORA_ENABLE_IMGUI=OFF).
// Real ImGui is not vendored on Switch; provide just enough surface so:
//   - dusk/imgui/*.hpp headers parse when pulled in by non-filtered TUs
//     (m_Do_main.cpp, m_Do_graphic.cpp, f_op_scene_req.cpp, d_kankyo.cpp,
//      d_bright_check.cpp, d_s_name.cpp, ...)
//   - d_camera.cpp's #if TARGET_PC fly-cam block (TARGET_PC is defined on
//     Switch too) compiles and links — calls fall through to no-ops.
// The dusk/imgui/*.cpp files themselves are filtered out of DUSK_FILES.

#include <cstddef>
#include <cstdint>

struct ImVec2 {
    float x = 0.0f;
    float y = 0.0f;
    constexpr ImVec2() = default;
    constexpr ImVec2(float _x, float _y) : x(_x), y(_y) {}
};

struct ImVec4 {
    float x = 0.0f, y = 0.0f, z = 0.0f, w = 0.0f;
    constexpr ImVec4() = default;
    constexpr ImVec4(float _x, float _y, float _z, float _w) : x(_x), y(_y), z(_z), w(_w) {}
};

class ImFont;
struct ImGuiWindow;
struct ImGuiContext;

using ImTextureID = std::uint64_t;
using ImGuiSliderFlags = int;
using ImGuiInputTextFlags = int;
using ImGuiWindowFlags = int;
using ImGuiCol = int;
using ImGuiID = unsigned int;

enum ImGuiKey : int {
    ImGuiKey_None = 0,
    ImGuiKey_Tab, ImGuiKey_LeftArrow, ImGuiKey_RightArrow, ImGuiKey_UpArrow, ImGuiKey_DownArrow,
    ImGuiKey_PageUp, ImGuiKey_PageDown, ImGuiKey_Home, ImGuiKey_End, ImGuiKey_Insert, ImGuiKey_Delete,
    ImGuiKey_Backspace, ImGuiKey_Space, ImGuiKey_Enter, ImGuiKey_Escape,
    ImGuiKey_LeftCtrl, ImGuiKey_LeftShift, ImGuiKey_LeftAlt, ImGuiKey_LeftSuper,
    ImGuiKey_RightCtrl, ImGuiKey_RightShift, ImGuiKey_RightAlt, ImGuiKey_RightSuper,
    ImGuiKey_A, ImGuiKey_B, ImGuiKey_C, ImGuiKey_D, ImGuiKey_E, ImGuiKey_F, ImGuiKey_G,
    ImGuiKey_H, ImGuiKey_I, ImGuiKey_J, ImGuiKey_K, ImGuiKey_L, ImGuiKey_M, ImGuiKey_N,
    ImGuiKey_O, ImGuiKey_P, ImGuiKey_Q, ImGuiKey_R, ImGuiKey_S, ImGuiKey_T, ImGuiKey_U,
    ImGuiKey_V, ImGuiKey_W, ImGuiKey_X, ImGuiKey_Y, ImGuiKey_Z,
    ImGuiKey_F1, ImGuiKey_F2, ImGuiKey_F3, ImGuiKey_F4, ImGuiKey_F5, ImGuiKey_F6,
    ImGuiKey_F7, ImGuiKey_F8, ImGuiKey_F9, ImGuiKey_F10, ImGuiKey_F11, ImGuiKey_F12,
};

using ImGuiConfigFlags = int;
inline constexpr ImGuiConfigFlags ImGuiConfigFlags_None = 0;
inline constexpr ImGuiConfigFlags ImGuiConfigFlags_NoMouseCursorChange = 1 << 0;

struct ImGuiIO {
    bool WantCaptureMouse = false;
    bool WantCaptureKeyboard = false;
    bool WantTextInput = false;
    ImGuiConfigFlags ConfigFlags = 0;
    ImVec2 MousePos{-1.0f, -1.0f};
    ImVec2 DisplaySize{0.0f, 0.0f};
    float DeltaTime = 0.0f;
    float Framerate = 0.0f;
};

struct ImGuiViewport {
    ImVec2 Pos{0.0f, 0.0f};
    ImVec2 Size{0.0f, 0.0f};
    ImVec2 WorkPos{0.0f, 0.0f};
    ImVec2 WorkSize{0.0f, 0.0f};
};

namespace ImGui {

inline ImGuiIO& GetIO() {
    static ImGuiIO io{};
    return io;
}

inline bool IsKeyDown(ImGuiKey) { return false; }
inline bool IsKeyPressed(ImGuiKey, bool = true) { return false; }
inline bool IsKeyReleased(ImGuiKey) { return false; }

inline bool Checkbox(const char*, bool*) { return false; }
inline bool SliderFloat(const char*, float*, float, float, const char* = "%.3f", ImGuiSliderFlags = 0) { return false; }
inline bool SliderInt(const char*, int*, int, int, const char* = "%d", ImGuiSliderFlags = 0) { return false; }
inline bool MenuItem(const char*, const char* = nullptr, bool* = nullptr, bool = true) { return false; }
inline bool MenuItem(const char*, const char*, bool, bool = true) { return false; }
inline bool Begin(const char*, bool* = nullptr, ImGuiWindowFlags = 0) { return false; }
inline void End() {}
inline void Text(const char*, ...) {}
inline void TextUnformatted(const char*, const char* = nullptr) {}
inline bool Button(const char*, const ImVec2& = ImVec2()) { return false; }

inline ImGuiViewport* GetMainViewport() {
    static ImGuiViewport vp{};
    return &vp;
}

}  // namespace ImGui
