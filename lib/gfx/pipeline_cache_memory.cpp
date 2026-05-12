#include "pipeline_cache.hpp"

#include "clear.hpp"
#include "../gx/pipeline.hpp"

#include <mutex>

#include <absl/container/flat_hash_map.h>
#include <tracy/Tracy.hpp>

namespace aurora::gfx {
namespace {
std::mutex g_pipelineMutex;
absl::flat_hash_map<PipelineRef, wgpu::RenderPipeline> g_pipelines;

template <typename PipelineConfig>
PipelineRef find_pipeline_impl(ShaderType type, const PipelineConfig& config, NewPipelineCallback&& cb) {
  ZoneScoped;
  const PipelineRef hash = xxh3_hash(config, static_cast<HashType>(type));
  {
    std::lock_guard lock{g_pipelineMutex};
    if (g_pipelines.contains(hash)) {
      return hash;
    }
  }

  auto pipeline = cb();
  {
    std::lock_guard lock{g_pipelineMutex};
    if (!g_pipelines.contains(hash)) {
      g_pipelines.emplace(hash, std::move(pipeline));
      ++g_stats.createdPipelines;
    }
  }
  return hash;
}
} // namespace

template <>
PipelineRef find_pipeline(ShaderType type, const clear::PipelineConfig& config, NewPipelineCallback&& cb) {
  return find_pipeline_impl(type, config, std::move(cb));
}

template <>
PipelineRef find_pipeline(ShaderType type, const gx::PipelineConfig& config, NewPipelineCallback&& cb) {
  return find_pipeline_impl(type, config, std::move(cb));
}

void initialize_pipeline_cache() {
  std::lock_guard lock{g_pipelineMutex};
  g_pipelines.clear();
  g_stats.queuedPipelines = 0;
  g_stats.createdPipelines = 0;
}

void shutdown_pipeline_cache() {
  std::lock_guard lock{g_pipelineMutex};
  g_pipelines.clear();
  g_stats.queuedPipelines = 0;
  g_stats.createdPipelines = 0;
}

void begin_pipeline_frame() {}
void end_pipeline_frame() {}

bool get_pipeline(PipelineRef ref, wgpu::RenderPipeline& pipeline) {
  std::lock_guard lock{g_pipelineMutex};
  const auto it = g_pipelines.find(ref);
  if (it == g_pipelines.end()) {
    return false;
  }
  pipeline = it->second;
  return true;
}
} // namespace aurora::gfx
