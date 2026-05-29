#pragma once
// Stub for ImGui's std::string helpers — only referenced by dusk/imgui/*
// headers on Switch where AURORA_ENABLE_IMGUI=OFF. The .cpp files that
// would actually call these are filtered from DUSK_FILES in CMakeLists.txt.
#include <string>
#include "imgui.h"

namespace ImGui {
inline bool InputText(const char*, std::string*, ImGuiInputTextFlags = 0) { return false; }
inline bool InputTextMultiline(const char*, std::string*, const ImVec2& = ImVec2(), ImGuiInputTextFlags = 0) { return false; }
inline bool InputTextWithHint(const char*, const char*, std::string*, ImGuiInputTextFlags = 0) { return false; }
}  // namespace ImGui
