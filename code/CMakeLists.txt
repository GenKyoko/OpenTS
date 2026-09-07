# Limit the generated solution to supported configurations.
set(CMAKE_CONFIGURATION_TYPES Debug Release CACHE STRING "" FORCE)

# OpenTS currently supports 32-bit (x86) builds only.
# if(NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
#     message(FATAL_ERROR "OpenTS must be built as 32-bit x86. Reconfigure with -A Win32.")
# endif()

#
# ---------------------------------------------------------
# Directories that build as their own project
# ---------------------------------------------------------
#
set(SUBPROJECT_DIRS
    "language"
    "vqalib"
)

#
# ---------------------------------------------------------
# Gather all potential OpenTS files
# ---------------------------------------------------------
#
file(GLOB_RECURSE OPENTS_RAW CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.hh"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.rc"
#    "${CMAKE_CURRENT_SOURCE_DIR}/*.asm"

    # Submodules of OpenTS
    "${CMAKE_CURRENT_SOURCE_DIR}/wolapi/*"
)

#
# ---------------------------------------------------------
# Remove files that belong to a subproject
# ---------------------------------------------------------
#
set(OPENTS_SRC "")
foreach(f ${OPENTS_RAW})
    set(EXCLUDED FALSE)
    foreach(dir ${SUBPROJECT_DIRS})
        if(f MATCHES "^${CMAKE_CURRENT_SOURCE_DIR}/${dir}/.*")
            set(EXCLUDED TRUE)
        endif()
    endforeach()

    # detproc.asm was ported to detproc.cpp; keep the file but do not build it.
    if(f STREQUAL "${CMAKE_CURRENT_SOURCE_DIR}/detproc.asm")
        set(EXCLUDED TRUE)
    endif()

    # isoasm.asm was ported to isoblit.cpp; keep the file but do not build it.
    if(f STREQUAL "${CMAKE_CURRENT_SOURCE_DIR}/isoasm.asm")
        set(EXCLUDED TRUE)
    endif()

    # olsosdec.asm was ported to olsosdec.cpp; keep the file but do not build it.
    if(f STREQUAL "${CMAKE_CURRENT_SOURCE_DIR}/olsosdec.asm")
        set(EXCLUDED TRUE)
    endif()

    # soscodec.asm was ported to soscodec.cpp; keep the file but do not build it.
    if(f STREQUAL "${CMAKE_CURRENT_SOURCE_DIR}/soscodec.asm")
        set(EXCLUDED TRUE)
    endif()

    # vqa_sos.asm was ported to vqa_sos.cpp; keep the file but do not build it.
    if(f STREQUAL "${CMAKE_CURRENT_SOURCE_DIR}/vqa_sos.asm")
        set(EXCLUDED TRUE)
    endif()

    # vqa_uncomp.asm was ported to vqa_uncomp.cpp; keep the file but do not build it.
    if(f STREQUAL "${CMAKE_CURRENT_SOURCE_DIR}/vqa_uncomp.asm")
        set(EXCLUDED TRUE)
    endif()

    # winasm.asm was ported to winasm.cpp; keep the file but do not build it.
    if(f STREQUAL "${CMAKE_CURRENT_SOURCE_DIR}/winasm.asm")
        set(EXCLUDED TRUE)
    endif()

    # unvq_asm.asm was ported to unvq_asm.cpp; keep the file but do not build it.
    if(f STREQUAL "${CMAKE_CURRENT_SOURCE_DIR}/unvq_asm.asm")
        set(EXCLUDED TRUE)
    endif()

    # smartdeform_.asm was ported to smartdeform_asm.cpp; keep the file but do not build it.
    if(f STREQUAL "${CMAKE_CURRENT_SOURCE_DIR}/smartdeform_.asm")
        set(EXCLUDED TRUE)
    endif()

    # auduncmp.asm was ported to vqa_uncomp.cpp (Decompress_Frame); keep the
    # file but do not build it.
    if(f STREQUAL "${CMAKE_CURRENT_SOURCE_DIR}/auduncmp.asm")
        set(EXCLUDED TRUE)
    endif()

    # cliprect.asm was ported to cliprect.cpp; keep the file but do not build it.
    if(f STREQUAL "${CMAKE_CURRENT_SOURCE_DIR}/cliprect.asm")
        set(EXCLUDED TRUE)
    endif()

    if(NOT EXCLUDED)
        list(APPEND OPENTS_SRC "${f}")
    endif()
endforeach()

#
# ---------------------------------------------------------
# Create actual OpenTS executable (Win32)
# ---------------------------------------------------------
#
message(STATUS "${PROJECT_NAME}: Creating '${PROJECT_NAME}' executable...")

add_executable(OpenTS WIN32 ${OPENTS_SRC})
target_compile_features(OpenTS PRIVATE cxx_std_20)

#
# ---------------------------------------------------------
# Output paths
# ---------------------------------------------------------
#
message(STATUS "${PROJECT_NAME}: Setting binary output paths...")

set_target_properties(OpenTS PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"

    # Built under the shipped names, so the executable, the symbol file the debug record
    # names, and the map reach the run directory without a rename.
    OUTPUT_NAME_DEBUG "GameD"
    OUTPUT_NAME_RELEASE "Game"
    PDB_NAME_DEBUG "GameD"
    PDB_NAME_RELEASE "Game"
)

set_target_properties(OpenTS PROPERTIES
    VS_DEBUGGER_COMMAND "${TS_RUN_DIR}/$<TARGET_FILE_NAME:OpenTS>"
    VS_DEBUGGER_WORKING_DIRECTORY "${TS_RUN_DIR}"
)

# The game loads the language resource DLL at runtime, so the language build finishes
# before the game links. Building the two in parallel has been seen to abort the
# language link outright, which left the copy into the run directory undone and the
# game running against a stale Language.dll.
add_dependencies(OpenTS Language)

#
# ---------------------------------------------------------
# Compile flags
# ---------------------------------------------------------
#
message(STATUS "${PROJECT_NAME}: Applying compiler flags...")

set(OPENTS_COMPILE_OPTIONS

    # ================= DEBUG =================
    $<$<CONFIG:Debug>:

        # ---------- C++ ----------
        # /arch:SSE2 -- IEEE-754 single precision, no x87 excess precision.
        # /fp:precise -- no reassociation of the engine's accumulations.
        $<$<COMPILE_LANGUAGE:CXX>:
            /Zi /Od /RTC1 /GR /MP /EHsc /Oy- /MTd /Zc:__cplusplus /arch:SSE2 /fp:precise
        >

        # ---------- C ----------
        $<$<COMPILE_LANGUAGE:C>:
            /Zi /Od /RTC1 /MP /Oy- /arch:SSE2 /fp:precise
        >

        # ---------- MASM ----------
        $<$<COMPILE_LANGUAGE:ASM_MASM>:
            /Zi /c /coff /Cx /safeseh
        >
    >

    # ================= RELEASE =================
    $<$<CONFIG:Release>:

        # ---------- C++ ----------
        # /arch:SSE2 -- IEEE-754 single precision, no x87 excess precision.
        # /fp:precise -- no reassociation of the engine's accumulations.
        $<$<COMPILE_LANGUAGE:CXX>:
            /Zi /O2 /GF /GR /MP /EHsc /MT /Zc:__cplusplus /arch:SSE2 /fp:precise
        >

        # ---------- C ----------
        $<$<COMPILE_LANGUAGE:C>:
            /Zi /O2 /GF /MP /arch:SSE2 /fp:precise
        >

        # ---------- MASM ----------
        $<$<COMPILE_LANGUAGE:ASM_MASM>:
            /Zi /c /coff /Cx /safeseh
        >
    >
)

target_compile_options(OpenTS PRIVATE ${OPENTS_COMPILE_OPTIONS})

#
# ---------------------------------------------------------
# Subprojects
# ---------------------------------------------------------
#
# The VQA player carries its own project, and hands OpenTS its include path along with the
# library, so the movie code is reached the same way from anywhere in the solution.
add_subdirectory(vqalib)

message(STATUS "${PROJECT_NAME}: Adding include paths...")
target_include_directories(OpenTS PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${OPENTS_GENERATED_DIR}"
)

# The generated build stamp has to exist before anything compiles.
add_dependencies(OpenTS OpenTSBuildStamp)

message(STATUS "${PROJECT_NAME}: Adding compilier definitions...")
target_compile_definitions(OpenTS PRIVATE
    WIN32
    _WINDOWS
    _MBCS

    # Compiles Blowfish into the binary instead of reaching it through the COM object in
    # blowfish.dll. It decides the layout of BlowfishEngine, so every translation unit has
    # to agree on it and it belongs on the compile line rather than in a header.
    NO_BLOWFISH_DLL
)

#
# ---------------------------------------------------------
# Windows libraries and linker options
# ---------------------------------------------------------
#
message(STATUS "${PROJECT_NAME}: Applying linker options...")

# Required libraries:
#  Comctl32.lib
#  DbgHelp.lib
#  DSound.lib
#  Version.lib
#  Winmm.lib
#  wsock32.lib
#  + all Win32 libs
#

# The renderer lives in renderbackend.cpp and links SDL3, which is set up further down
# alongside the engine's other third party dependencies.
target_link_libraries(OpenTS PRIVATE
    VQALib

    comctl32
    dbghelp
    dsound
    iphlpapi
    psapi
    version
    winmm
    wsock32
    kernel32 user32 gdi32 imm32 winspool comdlg32 advapi32 shell32
    ole32 oleaut32 uuid odbc32 odbccp32
)

#
# ---------------------------------------------------------
# HarfBuzz shaping for the wide text path
# ---------------------------------------------------------
#
# The wide text path (widetext.cpp) rasterizes through FreeType. With HarfBuzz the
# shaping stage resolves the text into real font glyph ids, which fixes ligatures,
# joining forms and glyph substitutions. The sources live in thirdparty/harfbuzz and
# thirdparty/CMakeLists.txt builds them with the FreeType backend, whose glyph ids
# pair against the FreeType rasterization.
option(OPENTS_WITH_HARFBUZZ "Shape the wide text path with HarfBuzz" ON)

if (OPENTS_WITH_HARFBUZZ)
    if (NOT TARGET harfbuzz)
        message(FATAL_ERROR "OPENTS_WITH_HARFBUZZ is set, but the harfbuzz target is missing. Check that thirdparty/harfbuzz is populated.")
    endif()
    target_link_libraries(OpenTS PRIVATE harfbuzz freetype)
    target_include_directories(OpenTS PRIVATE
        "${CMAKE_SOURCE_DIR}/thirdparty/harfbuzz/src"
        "${CMAKE_SOURCE_DIR}/thirdparty/freetype/include")
    target_compile_definitions(OpenTS PRIVATE OPENTS_WITH_HARFBUZZ)
endif()

#
# ---------------------------------------------------------
# AVX2 code generation
# ---------------------------------------------------------
#
# Compiles the engine's own C/C++ sources with AVX2 code generation enabled. The
# thirdparty libraries keep their own instruction sets, and the MASM assembly files
# are unaffected. The resulting executable requires a CPU with AVX2 support (Intel
# Haswell 2013+ / AMD Excavator 2015+ or newer); turn this off with
# -DOPENTS_USE_AVX2=OFF to build for older processors.
option(OPENTS_USE_AVX2 "Generate AVX2 code for the engine's C/C++ sources" OFF)

if (OPENTS_USE_AVX2)
    if (MSVC)
        #
        # All assembly sources were ported to C++ and none are part of the build, so
        # the flag landing on the MASM rule of the generated project is harmless --
        # ml.exe is never invoked.
        #
        target_compile_options(OpenTS PRIVATE /arch:AVX2)
    else()
        target_compile_options(OpenTS PRIVATE -mavx2)
    endif()
endif()

#
# ---------------------------------------------------------
# jsoncpp for JSON data files
# ---------------------------------------------------------
#
# The static library from thirdparty/jsoncpp is linked so the engine can read JSON
# data files (<json/json.h>). Built in thirdparty/CMakeLists.txt with tests, examples
# and packaging disabled.
if (NOT TARGET jsoncpp_static)
    message(FATAL_ERROR "The jsoncpp_static target is missing. Check that thirdparty/jsoncpp is populated.")
endif()
target_link_libraries(OpenTS PRIVATE jsoncpp_static)
target_include_directories(OpenTS PRIVATE "${CMAKE_SOURCE_DIR}/thirdparty/jsoncpp/include")

#
# ---------------------------------------------------------
# SDL3 for windowing, input and events
# ---------------------------------------------------------
#
# The static library from thirdparty/SDL is linked so the engine can use SDL3
# (<SDL3/SDL.h>). Built in thirdparty/CMakeLists.txt with the shared library,
# tests and installation disabled; the target carries its own include path.
if (NOT TARGET SDL3::SDL3-static)
    message(FATAL_ERROR "The SDL3::SDL3-static target is missing. Check that thirdparty/SDL is populated.")
endif()
target_link_libraries(OpenTS PRIVATE SDL3::SDL3-static)

if (MSVC)
    target_link_options(OpenTS PRIVATE
        /SUBSYSTEM:WINDOWS
        /DEBUG
        /MAP
        # Raises the user address space from 2GB to 4GB under 64-bit Windows. Pointers may
        # then have the high bit set, so anything holding one in a signed integer must only
        # round-trip the bits and never compare or scale them.
        /LARGEADDRESSAWARE
        # Declares the process per-monitor DPI aware, so a scaled desktop does not blur
        # the frame by stretching the whole window.
        "/MANIFESTINPUT:${CMAKE_CURRENT_SOURCE_DIR}/sun.manifest"
        /MANIFEST:EMBED
    )
endif()

#
# ---------------------------------------------------------
# MASM assembly support
# ---------------------------------------------------------
#
message(STATUS "${PROJECT_NAME}: Adding MASM support...")

enable_language(ASM_MASM)

# All .asm files compile automatically using ML.EXE with appropriate flags.
foreach(f ${OPENTS_SRC})
    if(f MATCHES "\\.asm$")
        set_source_files_properties(${f} PROPERTIES
            LANGUAGE ASM_MASM
            COMPILE_FLAGS "-Zi -c -coff -Cx"
        )
    endif()
endforeach()

#
# ---------------------------------------------------------
# Visual Studio source groups
# ---------------------------------------------------------
#
message(STATUS "${PROJECT_NAME}: Setting up Visual Studio filters...")

# Init Source Files (_*.cpp)
foreach(f ${OPENTS_SRC})
    if(f MATCHES "[/\\\\]_.*\\.cpp$")
        source_group("Source Files\\Init Source Files" FILES ${f})
    endif()
endforeach()

# Init Header Files (_*.h)
foreach(f ${OPENTS_SRC})
    if(f MATCHES "[/\\\\]_.*\\.h$")
        source_group("Header Files\\Init Header Files" FILES ${f})
    endif()
endforeach()

# Define Header Files (*.hh)
foreach(f ${OPENTS_SRC})
    if(f MATCHES "\\.hh$")
        source_group("Header Files\\Define Header Files" FILES ${f})
    endif()
endforeach()

# Assembly Files
foreach(f ${OPENTS_SRC})
    if(f MATCHES "\\.asm$")
        source_group("Assembly Files" FILES ${f})
    endif()
endforeach()

# Resource Files
foreach(f ${OPENTS_SRC})
    if(f MATCHES "\\.(ico|cur|bmp|gif|jpe?g|rc)$")
        source_group("Resource Files" FILES ${f})
    endif()
endforeach()

# List of all interface filenames (headers + C stubs)
set(INTERFACE_FILES
    iblockci.h       iblockci_i.c
    iblowfish.h      iblowfish_i.c
    iflyctrl.h       iflyctrl_i.c
    ilinkstm.h
    iloco.h          iloco_i.c
    ilocos.h         ilocos_i.c
    ipiggy.h         ipiggy_i.c
    isun.h           isun_i.c
    wolapi/wolapi_i.c
)

# Convert to full paths relative to source dir
set(FULL_INTERFACE_FILES "")
foreach(f IN LISTS INTERFACE_FILES)
    list(APPEND FULL_INTERFACE_FILES "${CMAKE_CURRENT_SOURCE_DIR}/${f}")
endforeach()

# Add them to the "Interface Files" group and exclude *_i.c from build
foreach(f IN LISTS FULL_INTERFACE_FILES)

    # Put into VS filter
    source_group("Interface Files" FILES "${f}")

    # Exclude *_i.c from build, but DO NOT mark headers as header-only
    if(f MATCHES "_i\\.c$")
        set_source_files_properties("${f}" PROPERTIES HEADER_FILE_ONLY TRUE)
    endif()

endforeach()

# General source files
source_group("Source Files" REGULAR_EXPRESSION ".*\\.(c|cpp)$")
source_group("Header Files" REGULAR_EXPRESSION ".*\\.(h|hpp)$")

#
# ---------------------------------------------------------
# Populate the local run directory
# ---------------------------------------------------------
#
message(STATUS "${PROJECT_NAME}: Creating post-build commands...")
add_custom_command(TARGET OpenTS POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory
        "${TS_RUN_DIR}"
)

add_custom_command(TARGET OpenTS POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:OpenTS>"
        "${TS_RUN_DIR}"
)

# Refresh the language resource DLL alongside the game. The stock Language.dll
# (version 5.43) has been seen overwriting the run directory, which silently drops
# every custom string; refreshing it on every game build restores the built one even
# when the language target itself does not relink.
add_custom_command(TARGET OpenTS POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:Language>"
        "${TS_RUN_DIR}/$<TARGET_FILE_NAME:Language>"
)

# Copy the linker-generated .pdb alongside the exe
add_custom_command(TARGET OpenTS POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_PDB_FILE:OpenTS>"
        "${TS_RUN_DIR}"
)

# Copy the linker-generated .map (sits next to the exe, no GenEx for it)
add_custom_command(TARGET OpenTS POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE_DIR:OpenTS>/$<TARGET_FILE_BASE_NAME:OpenTS>.map"
        "${TS_RUN_DIR}"
)

#
# ---------------------------------------------------------
# Visual Studio startup project
# ---------------------------------------------------------
#
set_property(GLOBAL PROPERTY VS_STARTUP_PROJECT OpenTS)

# ---------------------------------------------------------
# Add the .natvis file for Visual Studio debugging
# ---------------------------------------------------------
set(NATVIS_FILE "${CMAKE_CURRENT_SOURCE_DIR}/sun.natvis")
source_group("Natvis Files" FILES ${NATVIS_FILE})
target_sources(OpenTS PRIVATE ${NATVIS_FILE})
target_link_options(OpenTS PRIVATE "/NATVIS:${NATVIS_FILE}")
