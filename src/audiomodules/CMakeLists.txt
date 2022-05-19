cmake_minimum_required(VERSION 3.10)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED True)

include_directories(claphosting ../../src/include ../../../Rack-SDK/include ../../../Rack-SDK/include/simd ../../../Rack-SDK/include/dsp ../../../Rack-SDK/dep/include)

project(GRLOOPER)

add_executable(GRLOOPER rtmidi/RtMidi.cpp ../dr_wav.c ../grain_engine.cpp granularmodule.cpp)
add_executable(KLANG rtmidi/RtMidi.cpp scaleosc.cpp)
target_compile_definitions (GRLOOPER PRIVATE RAPIHEADLESS __LINUX_ALSA__)
target_compile_definitions (KLANG PRIVATE RAPIHEADLESS __LINUX_ALSA__)
target_compile_options(GRLOOPER PRIVATE -O2)
target_compile_options(KLANG PRIVATE -O2)
target_link_libraries (GRLOOPER PRIVATE portaudio ncurses asound pthread jansson)
target_link_libraries (KLANG PRIVATE portaudio ncurses asound pthread)
