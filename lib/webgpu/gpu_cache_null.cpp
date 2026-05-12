#include "gpu.hpp"

namespace aurora::webgpu {
size_t load_from_cache(void const*, size_t, void*, size_t, void*) { return 0; }
void store_to_cache(void const*, size_t, void const*, size_t, void*) {}
void cache_shutdown() {}
} // namespace aurora::webgpu
