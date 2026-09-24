CXX ?= g++

EXTRA_FLAGS =
LUA_PKG ?= $(shell pkg-config --exists 'lua5.4 >= 5.4' && echo lua5.4 || echo lua)
VERSION_HEADER = .build/PluginVersion.hpp
VERSION_SCRIPT = scripts/generate-plugin-version.sh

# OUT lets test builds land somewhere other than the path the desktop loads,
# e.g. `make OUT=.build/dev/spatialoverview.so`.
OUT ?= spatialoverview.so
OBJDIR = .build/obj

SOURCES = main.cpp BarrelShader.cpp Config.cpp DropIndicator.cpp Experiments.cpp Hud.cpp Icons.cpp Memory.cpp Navigator.cpp OverviewGesture.cpp OverviewManager.cpp \
          OverviewPassElement.cpp OverviewRender.cpp Popups.cpp Tuning.cpp Window.cpp scrollOverview.cpp
OBJECTS = $(SOURCES:%.cpp=$(OBJDIR)/%.o)
PKGS    = pixman-1 libdrm hyprland pangocairo libinput libudev wayland-server xkbcommon '$(LUA_PKG) >= 5.4'
CXXFLAGS_ALL = -fPIC $(EXTRA_FLAGS) -I.build -g -std=c++2b -Wno-narrowing `pkg-config --cflags $(PKGS)`

ifeq ($(CXX),g++)
    EXTRA_FLAGS += -fno-gnu-unique
endif

.PHONY: all clean safe-unload test-tools FORCE

all: $(OBJECTS)
	@mkdir -p $(dir $(OUT))
	$(CXX) -shared -fPIC $(EXTRA_FLAGS) $(OBJECTS) -o $(OUT).next -g `pkg-config --libs pangocairo hyprgraphics`
	mv -f $(OUT).next $(OUT)

# The lens shader is compiled into the plugin (#embed in BarrelShader.cpp).
$(OBJDIR)/BarrelShader.o: shaders/barrel.frag

$(OBJDIR)/%.o: %.cpp $(VERSION_HEADER) $(wildcard *.hpp)
	@mkdir -p $(OBJDIR)
	$(CXX) -c $(CXXFLAGS_ALL) $< -o $@

$(VERSION_HEADER): FORCE $(VERSION_SCRIPT)
	sh $(VERSION_SCRIPT) $@ .

FORCE:

# A scripted virtual mouse for the nested tests (tests/tools/vpointer.c).
VPOINTER = .build/vpointer
VPOINTER_PROTOCOL = tests/tools/wlr-virtual-pointer-unstable-v1.xml

test-tools: $(VPOINTER)

$(VPOINTER): tests/tools/vpointer.c $(VPOINTER_PROTOCOL)
	@mkdir -p .build/vpointer-gen
	wayland-scanner client-header $(VPOINTER_PROTOCOL) .build/vpointer-gen/wlr-virtual-pointer-unstable-v1-client-protocol.h
	wayland-scanner private-code $(VPOINTER_PROTOCOL) .build/vpointer-gen/wlr-virtual-pointer-unstable-v1-protocol.c
	$(CC) -O2 -I.build/vpointer-gen $< .build/vpointer-gen/wlr-virtual-pointer-unstable-v1-protocol.c -lwayland-client -lm -o $@

# Helper that install-live.sh loads to unload the running build safely.
SAFE_UNLOAD = .build/safe-unload.so

safe-unload: $(SAFE_UNLOAD)

$(SAFE_UNLOAD): scripts/safe-unload.cpp
	@mkdir -p $(dir $@)
	$(CXX) -shared $(CXXFLAGS_ALL) $< -o $@.next
	mv -f $@.next $@

clean:
	rm -rf $(OBJDIR) ./scrolloverview.so ./spatialoverview.so ./spatialoverview.so.next $(VERSION_HEADER) $(SAFE_UNLOAD)
