// Switch link stubs.
//
// On Switch the build enables GX but NOT ImGui (AURORA_ENABLE_IMGUI=OFF, so lib/imgui.cpp is not
// compiled), yet aurora.cpp guards its ImGui calls with AURORA_ENABLE_GX rather than
// AURORA_ENABLE_IMGUI, so those calls are present and must resolve. lib/system_info.cpp is likewise
// not built on Switch (its OS-info code only has Win/Apple/Linux branches), but
// aurora_initialize() calls log_system_information() unconditionally. Provide inert definitions so
// the final link succeeds.

#include "imgui.hpp"
#include "system_info.hpp"

namespace aurora::imgui {
void create_context() noexcept {}
void initialize() noexcept {}
void shutdown() noexcept {}
void new_frame(const AuroraWindowSize&) noexcept {}
DrawData freeze() noexcept { return DrawData{}; }
void render(const wgpu::RenderPassEncoder&, const DrawData&) noexcept {}
} // namespace aurora::imgui

namespace aurora {
void log_system_information() {}
} // namespace aurora
