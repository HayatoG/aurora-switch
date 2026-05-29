#pragma once

namespace tracy {
enum class PlotFormatType {
  Number,
  Memory,
  Percentage,
};
namespace Color {
// Vanilla Dusklight calls `ZoneScopedC(tracy::Color::Red4)`. Stub provides any integer value
// for the names so the (no-op) macro expansion compiles; unused at runtime.
inline constexpr unsigned Red4 = 0, Yellow4 = 0, Green4 = 0, Blue4 = 0, Magenta4 = 0, Cyan4 = 0,
                          Orange4 = 0, Purple4 = 0, White = 0, Gray = 0, Black = 0;
} // namespace Color
using ColorType = unsigned;
inline void SetThreadName(const char*) {}
} // namespace tracy

#define ZoneScoped ((void)0)
#define ZoneScopedN(name) ((void)0)
#define ZoneScopedS(depth) ((void)0)
#define ZoneScopedC(color) ((void)0)
#define ZoneScopedNC(name, color) ((void)0)
#define ZoneText(text, size) ((void)0)
#define ZoneName(name, size) ((void)0)
#define TracyPlotConfig(name, format, step, fill, color) ((void)0)
#define TracyPlot(name, value) ((void)0)
#define FrameMark ((void)0)
#define FrameMarkNamed(name) ((void)0)
#define TracyLockable(type, varname) type varname
#define TracyLockableN(type, varname, desc) type varname
#define LockableBase(type) type
#define LockMark(varname) ((void)0)
