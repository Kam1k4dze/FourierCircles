cmake_minimum_required(VERSION 3.25)

project(FourierCircles LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION ON)
set(BUILD_SHARED_LIBS OFF)

if (EMSCRIPTEN)
    message(STATUS "Targeting WebAssembly (Emscripten)")

    set(CMAKE_EXECUTABLE_SUFFIX ".html")

    add_compile_options($<$<COMPILE_LANGUAGE:CXX>:-stdlib=libc++>)
    add_link_options($<$<LINK_LANGUAGE:CXX>:-stdlib=libc++>)

    add_link_options(
            "-sUSE_SDL=0"
            "-sWASM=1"
            "-sALLOW_MEMORY_GROWTH=1"
            "-sUSE_WEBGL2=1"
            "-sMIN_WEBGL_VERSION=2"
            "-sMAX_WEBGL_VERSION=2"
            "-sEXPORTED_FUNCTIONS=['_main','_emscripten_file_selected']"
            "-sEXPORTED_RUNTIME_METHODS=['ccall','cwrap','FS']"
            "--shell-file" "${CMAKE_CURRENT_SOURCE_DIR}/web/shell.html"
    )
else ()
    if (CMAKE_C_COMPILER_ID STREQUAL "Clang" OR CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
        message(STATUS "Using Clang Native")
        set(USE_CLANG TRUE)
        add_compile_options($<$<COMPILE_LANGUAGE:CXX>:-stdlib=libc++>)
        add_link_options($<$<LINK_LANGUAGE:CXX>:-stdlib=libc++>)
    endif ()
endif ()

include(FetchContent)

FetchContent_Declare(
        NanoSVG
        GIT_REPOSITORY https://github.com/memononen/nanosvg.git
        GIT_TAG 5cefd9847949af6df13f65027fd43af5a7513633
        GIT_SHALLOW TRUE
        EXCLUDE_FROM_ALL
        SYSTEM
)
FetchContent_MakeAvailable(NanoSVG)

set(SDL_STATIC ON)
set(SDL_SHARED OFF)

FetchContent_Declare(
        SDL3
        GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
        GIT_TAG release-3.2.24
        GIT_SHALLOW TRUE
        EXCLUDE_FROM_ALL
        SYSTEM
)
FetchContent_MakeAvailable(SDL3)

if (CMAKE_CROSSCOMPILING)
    set(HOST_TOOLS_DIR "${CMAKE_CURRENT_BINARY_DIR}/host_tools")

    if (NOT EXISTS "${HOST_TOOLS_DIR}/embed_binary")
        message(STATUS "Building host tools for cross-compilation...")
        file(MAKE_DIRECTORY "${HOST_TOOLS_DIR}")

        execute_process(
                COMMAND ${CMAKE_COMMAND}
                -DCMAKE_CXX_STANDARD=23
                -DCMAKE_BUILD_TYPE=Release
                -S "${CMAKE_CURRENT_SOURCE_DIR}/tools"
                -B "${HOST_TOOLS_DIR}"
                RESULT_VARIABLE config_result
        )

        if (config_result)
            message(FATAL_ERROR "Failed to configure host tools")
        endif ()

        execute_process(
                COMMAND ${CMAKE_COMMAND} --build "${HOST_TOOLS_DIR}"
                RESULT_VARIABLE build_result
        )

        if (build_result)
            message(FATAL_ERROR "Failed to build host tools")
        endif ()
    endif ()

    set(EMBED_BINARY_EXE "${HOST_TOOLS_DIR}/embed_binary")
    set(EMBED_TEXT_EXE "${HOST_TOOLS_DIR}/embed_text")
else ()
    add_executable(embed_binary tools/embed_binary.cpp)
    add_executable(embed_text tools/embed_text.cpp)
    set_target_properties(embed_binary embed_text PROPERTIES
            CXX_STANDARD 23
            CXX_STANDARD_REQUIRED ON
    )
    set(EMBED_BINARY_EXE $<TARGET_FILE:embed_binary>)
    set(EMBED_TEXT_EXE $<TARGET_FILE:embed_text>)
endif ()

set(EMBEDDED_DIR "${CMAKE_CURRENT_BINARY_DIR}/embedded")
file(MAKE_DIRECTORY ${EMBEDDED_DIR})

add_custom_command(
        OUTPUT ${EMBEDDED_DIR}/embedded_font.h
        COMMAND ${EMBED_BINARY_EXE}
        "${CMAKE_CURRENT_SOURCE_DIR}/fonts/RobotoSlab-Medium.ttf"
        "${EMBEDDED_DIR}/embedded_font.h"
        "embedded_font_data"
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/fonts/RobotoSlab-Medium.ttf
        COMMENT "Embedding font into header..."
)

add_custom_command(
        OUTPUT ${EMBEDDED_DIR}/embedded_svg.h
        COMMAND ${EMBED_TEXT_EXE}
        "${CMAKE_CURRENT_SOURCE_DIR}/svg/cat.svg"
        "${EMBEDDED_DIR}/embedded_svg.h"
        "default_svg_content"
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/svg/cat.svg
        COMMENT "Embedding default SVG into header..."
)

add_custom_target(embedded_resources
        DEPENDS
        ${EMBEDDED_DIR}/embedded_font.h
        ${EMBEDDED_DIR}/embedded_svg.h
)

if (NOT CMAKE_CROSSCOMPILING)
    add_dependencies(embedded_resources embed_binary embed_text)
endif ()

file(GLOB_RECURSE PROJECT_SOURCES
        "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
)

add_executable(${PROJECT_NAME} ${PROJECT_SOURCES})
add_custom_target(shell_html DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/web/shell.html")
add_dependencies(${PROJECT_NAME} shell_html embedded_resources)
target_include_directories(${PROJECT_NAME} PRIVATE
        "${NanoSVG_SOURCE_DIR}/src"
        "${EMBEDDED_DIR}"
)
target_link_libraries(${PROJECT_NAME} PRIVATE SDL3::SDL3)

