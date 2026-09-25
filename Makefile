CXX      ?= g++
CXXFLAGS ?= -O2 -g
CXXFLAGS += -std=c++26 -shared -fPIC --no-gnu-unique -Wall -Wextra -Wno-unused-parameter \
            $(shell pkg-config --cflags pixman-1 libdrm hyprland pangocairo libinput libudev wayland-server xkbcommon)

SRC = $(wildcard src/*.cpp)
HDR = $(wildcard src/*.hpp)

all: hyprsubs.so

hyprsubs.so: $(SRC) $(HDR)
	$(CXX) $(CXXFLAGS) $(SRC) -o $@

clean:
	rm -f hyprsubs.so

.PHONY: all clean
