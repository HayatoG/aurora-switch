#pragma once

namespace tracy {
enum class PlotFormatType {
  Number,
  Memory,
  Percentage,
};
inline void SetThreadName(const char*) {}
} // namespace tracy

#define ZoneScoped ((void)0)
#define ZoneScopedN(name) ((void)0)
#define ZoneScopedS(depth) ((void)0)
#define TracyPlotConfig(name, format, step, fill, color) ((void)0)
#define TracyPlot(name, value) ((void)0)
