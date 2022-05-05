CC:= clang
CXX:= clang++
CXXFLAGS += -std=c++11
CXXFLAGS += -I../../src/include
CXXFLAGS += -I../../../Rack-SDK/include
CXXFLAGS += -I../../../Rack-SDK/include/simd
CXXFLAGS += -I../../../Rack-SDK/dep/include
CXXFLAGS += -O2

CXXFLAGS += -DRAPIHEADLESS

SOURCES += scaleosc.cpp

LDFLAGS += portaudio
LDFLAGS += curses

klang: 
	g++ $(CXXFLAGS) $(SOURCES) -oklangcli -lportaudio -lcurses



