if (AURORA_PLATFORM_SWITCH)
  add_library(aurora_dvd STATIC lib/dolphin/dvd/dvd_switch.cpp)
else ()
  include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/AuroraNodProvider.cmake)
  add_library(aurora_dvd STATIC lib/dolphin/dvd/dvd.cpp)
endif ()
add_library(aurora::dvd ALIAS aurora_dvd)
set_target_properties(aurora_dvd PROPERTIES FOLDER "aurora")

target_compile_definitions(aurora_dvd PUBLIC AURORA TARGET_PC)
target_include_directories(aurora_dvd PUBLIC include)
if (NOT AURORA_PLATFORM_SWITCH)
  target_link_libraries(aurora_dvd PUBLIC nod::nod ${AURORA_SDL3_TARGET})
endif ()
target_link_libraries(aurora_dvd PRIVATE fmt::fmt)
if (AURORA_PLATFORM_SWITCH)
  # devkitPro portlibs ship the cross-compiled headers + static libs for these compression deps,
  # but the Switch.cmake toolchain doesn't put their include dir on the path by default.
  # Add it explicitly so <bzlib.h>, <lzma.h>, <zstd.h>, <mbedtls/aes.h> resolve.
  target_include_directories(aurora_dvd PRIVATE
    "$ENV{DEVKITPRO}/portlibs/switch/include"
    /opt/devkitpro/portlibs/switch/include
  )
  target_link_directories(aurora_dvd PRIVATE
    "$ENV{DEVKITPRO}/portlibs/switch/lib"
    /opt/devkitpro/portlibs/switch/lib
  )
  target_link_libraries(aurora_dvd PRIVATE z zstd bz2 lzma mbedcrypto)
endif ()
