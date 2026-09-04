#
# ---------------------------------------------------------
# bgfx (rendering backend)
# ---------------------------------------------------------
#
if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/bgfx.cmake/CMakeLists.txt")
    message(FATAL_ERROR
        "thirdparty/bgfx.cmake is empty. Fetch the submodules with:\n"
        "  git submodule update --init --recursive")
endif()

# bgfx builds shader and texture tools by default; the engine ships neither.
set(BGFX_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
set(BGFX_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BGFX_INSTALL OFF CACHE BOOL "" FORCE)
set(BGFX_CONFIG_RENDERER_WEBGPU OFF CACHE BOOL "" FORCE)

# The engine links the static CRT, so its dependencies must agree.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

add_subdirectory(bgfx.cmake)

#
# ---------------------------------------------------------
# freetype (wide text rasterization)
# ---------------------------------------------------------
#
# Source lives in the tree (not a submodule). Every optional third party dependency is
# disabled: the wide text path only needs the core scaler, and HarfBuzz picks this
# freetype target up automatically for its own FreeType backend.
if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/freetype/CMakeLists.txt")
    message(FATAL_ERROR
        "thirdparty/freetype is missing. Fetch the freetype VER-2-14-3 sources into it.")
endif()

set(FT_DISABLE_ZLIB ON CACHE BOOL "" FORCE)
set(FT_DISABLE_BZIP2 ON CACHE BOOL "" FORCE)
set(FT_DISABLE_PNG ON CACHE BOOL "" FORCE)
set(FT_DISABLE_HARFBUZZ ON CACHE BOOL "" FORCE)
set(FT_DISABLE_BROTLI ON CACHE BOOL "" FORCE)

add_subdirectory(freetype EXCLUDE_FROM_ALL)

#
# ---------------------------------------------------------
# harfbuzz (wide text shaping)
# ---------------------------------------------------------
#
# Source lives in the tree (not a submodule). Only the core shaping library is built:
# the subset, raster, vector and GPU companions are unused. The freetype target above
# enables HarfBuzz's FreeType backend, which pairs HarfBuzz glyph ids with the
# FreeType rasterization of the wide text path.
if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/harfbuzz/CMakeLists.txt")
    message(FATAL_ERROR
        "thirdparty/harfbuzz is missing. Fetch the harfbuzz 14.4.0 sources into it.")
endif()

set(HB_BUILD_SUBSET OFF CACHE BOOL "" FORCE)
set(HB_BUILD_RASTER OFF CACHE BOOL "" FORCE)
set(HB_BUILD_VECTOR OFF CACHE BOOL "" FORCE)
set(HB_BUILD_GPU OFF CACHE BOOL "" FORCE)
set(HB_BUILD_GPU_DEMO OFF CACHE BOOL "" FORCE)
set(HB_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(HB_BUILD_UTILS OFF CACHE BOOL "" FORCE)
set(HB_HAVE_GDI OFF CACHE BOOL "" FORCE)
set(HB_HAVE_GLIB OFF CACHE BOOL "" FORCE)
set(HB_HAVE_ICU OFF CACHE BOOL "" FORCE)

add_subdirectory(harfbuzz EXCLUDE_FROM_ALL)
