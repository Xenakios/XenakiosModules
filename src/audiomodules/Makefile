CC:= clang
CXX:= clang++
CXXFLAGS += -std=c++11
CXXFLAGS += -I../../src/include
CXXFLAGS += -I../../../Rack-SDK2/include
CXXFLAGS += -I../../../Rack-SDK2/include/simd
CXXFLAGS += -I../../../Rack-SDK2/dep/include
CXXFLAGS += -O2

CXXFLAGS += -DRAPIHEADLESS

SOURCES += scaleosc.cpp

LDFLAGS += portaudio
LDFLAGS += curses

klang: 
	clang++ $(CXXFLAGS) $(SOURCES) -oklangcli -lportaudio -lcurses



