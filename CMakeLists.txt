cmake_minimum_required(VERSION 3.16)
cmake_policy(SET CMP0091 NEW)

set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

string(TIMESTAMP DAY_OF_YEAR "%j")
string(TIMESTAMP YEAR "%Y")
math(EXPR PART0 "${YEAR}")
math(EXPR PART1 "${DAY_OF_YEAR} + 1")

project(Xenakios VERSION 1.${PART0}.${PART1} LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_VISIBILITY_PRESET hidden)
set(CMAKE_VISIBILITY_INLINES_HIDDEN ON)
set(RACK_SDK_DIR "C:\\msys64\\home\\teemu\\Rack-SDK")

if(NOT MSVC)
    message(STATUS "Adding clang/gcc visibility options")
    add_compile_options(-fvisibility=hidden -fvisibility-inlines-hidden)
endif()

set(PLUGIN_NAME ${PROJECT_NAME})
set(ADDITIONAL_PLUGIN_DISTRIBUTABLES res LICENSE.md README.md)
include(RackSDK.cmake)

# add_subdirectory(libs/sst-rackhelpers)

# target_include_directories(${RACK_PLUGIN_LIB} PRIVATE src src-rack)
target_include_directories(${RACK_PLUGIN_LIB} PRIVATE src/include)
target_sources(${RACK_PLUGIN_LIB} PRIVATE
        src/plugin.cpp
        src/audiomodules/gendyn.cpp
        src/helperwidgets.cpp
        )

target_compile_options(${RACK_PLUGIN_LIB} PUBLIC -Wno-suggest-override -Wno-multichar -Wno-unused-value -Wno-unused-but-set-variable -Wno-unused-variable )

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU")
    target_compile_options(${RACK_PLUGIN_LIB} PUBLIC -Wno-stringop-truncation)
endif()

set(products_folder ${CMAKE_BINARY_DIR})
set(build_type ${CMAKE_BUILD_TYPE})
add_custom_command(
     TARGET ${RACK_PLUGIN_LIB} POST_BUILD
     COMMAND ${CMAKE_COMMAND} -E copy "${products_folder}/plugin.dll" "C:\\Users\\teemu\\AppData\\Local\\Rack2\\plugins-win-x64\\Xenakios"
)
