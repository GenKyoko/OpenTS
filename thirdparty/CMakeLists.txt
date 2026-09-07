#
# The engine links the static CRT, so its dependencies must agree.
#
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

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

#
# ---------------------------------------------------------
# jsoncpp (JSON parsing)
# ---------------------------------------------------------
#
# Source lives in the tree (not a submodule). Tests, examples and packaging are all
# unused; only the static library target (jsoncpp_lib_static) is built and linked.
if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/jsoncpp/CMakeLists.txt")
    message(FATAL_ERROR
        "thirdparty/jsoncpp is missing. Fetch the jsoncpp 1.9.8 sources into it.")
endif()

set(JSONCPP_WITH_TESTS OFF CACHE BOOL "" FORCE)
set(JSONCPP_WITH_POST_BUILD_UNITTEST OFF CACHE BOOL "" FORCE)
set(JSONCPP_WITH_EXAMPLE OFF CACHE BOOL "" FORCE)
set(JSONCPP_WITH_PKGCONFIG_SUPPORT OFF CACHE BOOL "" FORCE)
set(JSONCPP_WITH_CMAKE_PACKAGE OFF CACHE BOOL "" FORCE)
set(JSONCPP_WITH_WARNING_AS_ERROR OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(BUILD_STATIC_LIBS ON CACHE BOOL "" FORCE)
set(BUILD_OBJECT_LIBS OFF CACHE BOOL "" FORCE)

add_subdirectory(jsoncpp EXCLUDE_FROM_ALL)

#
# ---------------------------------------------------------
# SDL3 (windowing, input and events)
# ---------------------------------------------------------
#
# Source lives in the tree (cloned from libsdl-org/SDL, release-3.4.16). Only the
# static library is built: the engine links the static CRT, and SDL3 picks that up
# through CMAKE_MSVC_RUNTIME_LIBRARY, which is set above the bgfx block. Tests,
# examples and installation are all unused.
if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/SDL/CMakeLists.txt")
    message(FATAL_ERROR
        "thirdparty/SDL is missing. Clone the SDL3 release-3.4.16 sources into it:\n"
        "  git clone --depth 1 --branch release-3.4.16 https://github.com/libsdl-org/SDL thirdparty/SDL")
endif()

set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDL_INSTALL OFF CACHE BOOL "" FORCE)
set(SDL_WERROR OFF CACHE BOOL "" FORCE)

# The precompiled header races the parallel compilation of the SDL sources and fails
# with C1083 (cannot open cmake_pch.cxx.pch) nondeterministically. The PCH saves
# nothing worth the flake, so it is off.
set(SDL_DISABLE_PRECOMPILED_HEADERS ON CACHE BOOL "" FORCE)

add_subdirectory(SDL EXCLUDE_FROM_ALL)

# MSBuild's parallel compile nodes contend on SDL3-static.pdb and abort with C1041.
# /FS makes the compiler serialize the PDB writes, which is all the error asks for.
if(TARGET SDL3-static)
    target_compile_options(SDL3-static PRIVATE "/FS")
endif()
