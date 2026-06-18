cmake_minimum_required(VERSION 3.25)
project(aurora LANGUAGES C CXX)
if (APPLE)
    enable_language(OBJC)
endif()
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

option(AURORA_ENABLE_GX "Enable GX implementation and WebGPU renderer" ON)
option(AURORA_ENABLE_DVD "Enable DVD implementation backed by nod" OFF)  # TODO: need to fix tests
option(AURORA_ENABLE_CARD "Enable CARD implementation based on kabufuda" ON)
option(AURORA_ENABLE_RMLUI "Enable HTML/CSS Based UI Library for use in end-user UI development." OFF)
option(AURORA_CACHE_USE_ZSTD "Compress WebGPU cache entries with zstd" ON)
option(AURORA_PLATFORM_SWITCH "Use the libnx/NWindow platform backend instead of SDL3" OFF)

if (AURORA_PLATFORM_SWITCH)
  set(_aurora_default_imgui OFF)
  set(_aurora_default_gpu_cache OFF)
  # devkitPro ships every common Switch port-lib (libpng, bzip2, lzma, zstd, mbedtls, etc.) in
  # /opt/devkitpro/portlibs/switch/{include,lib}, but the Switch.cmake toolchain does NOT add
  # these dirs to the compile/link path by default. Add them globally so ANY aurora translation
  # unit that #include <png.h>, <bzlib.h>, <lzma.h>, etc. resolves without per-target plumbing.
  include_directories(SYSTEM
    "$ENV{DEVKITPRO}/portlibs/switch/include"
    /opt/devkitpro/portlibs/switch/include)
  link_directories(
    "$ENV{DEVKITPRO}/portlibs/switch/lib"
    /opt/devkitpro/portlibs/switch/lib)
  # v13's aurora_gx links PNG::PNG (lib/gfx/png_io.cpp for texture replacement). The Switch.cmake
  # toolchain never runs find_package(PNG), so define an IMPORTED PNG::PNG from the devkitPro
  # portlib (libpng16.a + zlib) so v13's aurora_gx.cmake resolves unchanged.
  if (NOT TARGET PNG::PNG)
    find_library(AURORA_SWITCH_PNG_LIBRARY NAMES png16 png
      PATHS "$ENV{DEVKITPRO}/portlibs/switch/lib" /opt/devkitpro/portlibs/switch/lib
      NO_DEFAULT_PATH)
    find_library(AURORA_SWITCH_ZLIB_LIBRARY NAMES z
      PATHS "$ENV{DEVKITPRO}/portlibs/switch/lib" /opt/devkitpro/portlibs/switch/lib
      NO_DEFAULT_PATH)
    if (NOT AURORA_SWITCH_PNG_LIBRARY)
      message(FATAL_ERROR "aurora: Missing Switch libpng dependency (portlibs)")
    endif ()
    add_library(PNG::PNG STATIC IMPORTED GLOBAL)
    set_target_properties(PNG::PNG PROPERTIES IMPORTED_LOCATION "${AURORA_SWITCH_PNG_LIBRARY}")
    if (AURORA_SWITCH_ZLIB_LIBRARY)
      set_target_properties(PNG::PNG PROPERTIES
        INTERFACE_LINK_LIBRARIES "${AURORA_SWITCH_ZLIB_LIBRARY}")
    endif ()
  endif ()
else ()
  set(_aurora_default_imgui ON)
  set(_aurora_default_gpu_cache ON)
endif ()
option(AURORA_ENABLE_IMGUI "Enable ImGui integration" ${_aurora_default_imgui})
option(AURORA_ENABLE_GPU_CACHE "Enable persistent WebGPU shader cache" ${_aurora_default_gpu_cache})

# Specifies a cache string and previous values to forcibly migrate from
macro(_aurora_dependency_version name value doc)
  set(_aurora_old_defaults ${ARGN})
  if (DEFINED CACHE{${name}} AND ${name} IN_LIST _aurora_old_defaults)
    message(STATUS "aurora: Migrating ${name} from old default ${${name}} to ${value}")
    set(${name} "${value}" CACHE STRING "${doc}" FORCE)
  else ()
    set(${name} "${value}" CACHE STRING "${doc}")
  endif ()
  unset(_aurora_old_defaults)
endmacro()

# Dependency versions
_aurora_dependency_version(AURORA_DAWN_VERSION "v20260618.032059" "Dawn prebuilt version tag (https://github.com/encounter/dawn/releases)"
        "v20260523.201736" "v20260603.191052") # Previous versions
_aurora_dependency_version(AURORA_DAWN_REF "266c1cf8de969a364afa4fa49311631fc99a881e" "Dawn commit ref (https://github.com/encounter/dawn)"
        "9aa45f938d4b36626722bbfdc2f18447179337e6" "13abc3bc8ea2d3c2050f9e77a12d012108ceee24") # Previous versions
_aurora_dependency_version(AURORA_SDL3_VERSION "3.4.10" "SDL3 prebuilt version tag (https://github.com/libsdl-org/SDL/releases)")
_aurora_dependency_version(AURORA_SDL3_REF "refs/tags/release-3.4.10" "SDL3 commit ref (https://github.com/libsdl-org/SDL)")
_aurora_dependency_version(AURORA_NOD_VERSION "v2.0.0-alpha.10" "nod version tag (https://github.com/encounter/nod/releases)")

# Platform-specific defaults
set(_default_provider "auto")
if (WIN32)
  set(_default_linkage "shared")
else ()
  set(_default_linkage "static")
endif ()

# Dawn/WebGPU (if AURORA_ENABLE_GX)
set(AURORA_DAWN_PROVIDER "${_default_provider}" CACHE STRING
  "How to provide Dawn: auto, vendor (download/build), source (local checkout), system (find_package/imported), package (download prebuilt)")
set_property(CACHE AURORA_DAWN_PROVIDER PROPERTY STRINGS auto vendor source system package)
set(AURORA_DAWN_LINKAGE "${_default_linkage}" CACHE STRING "Dawn linkage type preference")
set_property(CACHE AURORA_DAWN_LINKAGE PROPERTY STRINGS shared static)
set(AURORA_DAWN_SOURCE_DIR "" CACHE PATH "Local Dawn source directory when AURORA_DAWN_PROVIDER=source")

# SDL3
set(AURORA_SDL3_PROVIDER "${_default_provider}" CACHE STRING
  "How to provide SDL3: auto, vendor (build from source), system (find_package/imported), or package (download prebuilt)")
set_property(CACHE AURORA_SDL3_PROVIDER PROPERTY STRINGS auto vendor system package standalone)
set(AURORA_SDL3_LINKAGE "${_default_linkage}" CACHE STRING "SDL3 linkage type preference")
set_property(CACHE AURORA_SDL3_LINKAGE PROPERTY STRINGS shared static)

# nod (if AURORA_ENABLE_DVD)
set(AURORA_NOD_PROVIDER "${_default_provider}" CACHE STRING
  "How to provide nod: auto, vendor (build from source), system (find_package/imported), package (download prebuilt)")
set_property(CACHE AURORA_NOD_PROVIDER PROPERTY STRINGS auto vendor system package)
set(AURORA_NOD_LINKAGE "${_default_linkage}" CACHE STRING "nod linkage type preference")
set_property(CACHE AURORA_NOD_LINKAGE PROPERTY STRINGS shared static)

add_subdirectory(extern)

include(cmake/aurora_core.cmake)
include(cmake/aurora_os.cmake)
if (AURORA_ENABLE_GX)
  include(cmake/aurora_gx.cmake)
  include(cmake/aurora_gd.cmake)
endif ()
include(cmake/aurora_pad.cmake)
include(cmake/aurora_ms.cmake)
include(cmake/aurora_si.cmake)
include(cmake/aurora_main.cmake)
include(cmake/aurora_mtx.cmake)
include(cmake/aurora_vi.cmake)
if(AURORA_ENABLE_CARD)
  include(cmake/aurora_card.cmake)
endif()
if (AURORA_ENABLE_DVD)
  include(cmake/aurora_dvd.cmake)
endif ()

if (CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  add_subdirectory(examples)
endif ()

if (NOT CMAKE_CROSSCOMPILING)
  enable_testing()
  add_subdirectory(tests)
endif ()
