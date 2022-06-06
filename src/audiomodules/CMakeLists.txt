cmake_minimum_required(VERSION 3.10)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED True)

include_directories(claphosting /home/pi/codestuff/micro-clap-host/libs/clap/include ../../src/include ../../../Rack-SDK/include ../../../Rack-SDK/include/simd ../../../Rack-SDK/include/dsp ../../../Rack-SDK/dep/include)

project(GRLOOPER)

add_executable(GRLOOPER rtmidi/RtMidi.cpp ../grain_engine.cpp granularmodule.cpp claphosting/claphost.cpp)
add_executable(KLANG rtmidi/RtMidi.cpp scaleosc.cpp)
target_compile_definitions (GRLOOPER PRIVATE RAPIHEADLESS __LINUX_ALSA__)
target_compile_definitions (KLANG PRIVATE RAPIHEADLESS __LINUX_ALSA__)
target_compile_options(GRLOOPER PRIVATE -O2)
target_compile_options(KLANG PRIVATE -O2)
target_link_libraries (GRLOOPER PRIVATE portaudio ncurses asound pthread jansson dl sndfile)
target_link_libraries (KLANG PRIVATE portaudio ncurses asound pthread)
