CC:= clang
CXX:= clang++
CXXFLAGS += -std=c++11
CXXFLAGS += -I../../src/include
CXXFLAGS += -I../../../Rack-SDK/include
CXXFLAGS += -I../../../Rack-SDK/include/simd
CXXFLAGS += -I../../../Rack-SDK/include/dsp
CXXFLAGS += -I../../../Rack-SDK/dep/include
CXXFLAGS += -O2

CXXFLAGS += -DRAPIHEADLESS
CXXFLAGS += -DM_PI=3.14159265358979323846
CXXFLAGS += -DNCURSES_STATIC
CXXFLAGS += -D_M_ARM
# for mac 
# CXXFLAGS += -D__MACOSX_CORE__
# CXXFLAGS += -framework CoreMIDI -framework CoreAudio -framework CoreFoundation
# for linux
CXXFLAGS += -D__LINUX_ALSA__
# CXXFLAGS += -D__UNIX_JACK__
SOURCES += scaleosc.cpp
SOURCES += rtmidi/RtMidi.cpp
# LDFLAGS += portaudio
# LDFLAGS += curses

klang: 
	g++ $(CXXFLAGS) rtmidi/RtMidi.cpp scaleosc.cpp -oklangcli -lportaudio -lncurses -lasound -lpthread
grlooper: 
	g++ $(CXXFLAGS) rtmidi/RtMidi.cpp ../dr_wav.c ../grain_engine.cpp granularmodule.cpp -ogrloopercli -lportaudio -lncurses -lasound -lpthread

