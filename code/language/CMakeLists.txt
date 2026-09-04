file(GLOB_RECURSE LANG_SRC CONFIGURE_DEPENDS
    "*.cpp" "*.c" "*.cc" "*.rc"
    "*.h" "*.hpp" "*.hm"
)

add_library(Language SHARED ${LANG_SRC})
target_compile_features(Language PRIVATE cxx_std_20)

# The version resource is stamped from the generated headers, which the resource compiler
# reaches through the target's include paths.
target_include_directories(Language PRIVATE "${OPENTS_GENERATED_DIR}")
add_dependencies(Language OpenTSBuildStamp)

if (MSVC)
    target_link_options(Language PRIVATE "/NOENTRY" "/NODEFAULTLIB")
endif()

add_custom_command(TARGET Language POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${TS_RUN_DIR}"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:Language>"
        "${TS_RUN_DIR}/$<TARGET_FILE_NAME:Language>"
)

# The launcher picks one of the per-resolution language DLLs from the Resources
# directory and copies it over the run directory's Language.dll, which silently
# replaces the built strings with the stock ones. Overwrite those copies so every
# resolution loads the built strings.
set(TS_LANGUAGE_RESOURCES
    language_640x480
    language_800x600
    language_1024x720
)
foreach(_res_lang ${TS_LANGUAGE_RESOURCES})
    add_custom_command(TARGET Language POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:Language>"
            "${TS_RUN_DIR}/Resources/${_res_lang}.dll"
    )
endforeach()
