# Build Halcyon Setup against the LibGUI wayland host build produced by
# libgui-wayland (and installed by cports as serenity-lagom).
#
# Defaults point at a sibling libgui-wayland checkout:
#   make
#   ./scripts/run.sh --demo
#
# Override them for another checkout or for the cports build environment:
#   make LAGOM=/usr/lib/sde SERENITY=/usr/lib/sde/framework/serenity

LAGOM ?= $(abspath $(CURDIR)/../libgui-wayland/build/lagom)
SERENITY ?= $(abspath $(CURDIR)/../libgui-wayland/serenity)

CXX ?= c++
CXXFLAGS ?= -std=c++26 -fno-exceptions -O2 -g -Wall -Wextra -Wno-literal-suffix
CPPFLAGS += \
	-I$(SERENITY) \
	-I$(SERENITY)/Userland \
	-I$(SERENITY)/Userland/Libraries \
	-I$(SERENITY)/Userland/Services \
	-I$(LAGOM) \
	-I$(LAGOM)/Userland/Libraries \
	-I$(LAGOM)/Userland/Services \
	-I$(LAGOM)/framework \
	-I$(LAGOM)/framework/Userland/Libraries \
	-I$(LAGOM)/framework/Userland/Services \
	-I$(CURDIR)/src

LDFLAGS += -L$(LAGOM)/lib -Wl,--no-as-needed -Wl,-rpath,$(LAGOM)/lib -pthread
LDLIBS += \
	-llagom-gui \
	-llagom-gfx \
	-llagom-core \
	-llagom-coreminimal \
	-llagom-corebasic \
	-llagom-ipc \
	-llagom-main \
	-llagom-ak \
	-llagom-threading \
	-lwindowserver \
	-lm

SOURCES := \
	src/main.cpp \
	src/Installer/Command.cpp \
	src/Installer/Config.cpp \
	src/Installer/Devices.cpp \
	src/Installer/Installer.cpp \
	src/UI/Dialogs.cpp \
	src/UI/MainWindow.cpp \
	src/UI/Models.cpp \
	src/UI/WizardChrome.cpp \
	src/UI/WorldMap.cpp

OBJECTS := $(patsubst src/%.cpp,build/obj/%.o,$(SOURCES))
BIN := build/halcyon-setup
PREFIX ?= /usr/local

.PHONY: all clean run list-devices test ui-test install uninstall

all: $(BIN)

$(BIN): $(OBJECTS)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -o $@ $(OBJECTS) $(LDFLAGS) $(LDLIBS)

build/obj/%.o: src/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) -MMD -MP -c $< -o $@

-include $(OBJECTS:.o=.d)

run: $(BIN)
	./scripts/run.sh

list-devices: $(BIN)
	./build/halcyon-setup --list-devices

build/plan-test: tests/plan-test.cpp build/obj/Installer/Config.o build/obj/Installer/Devices.o build/obj/Installer/Command.o
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) -o $@ $^ $(LDFLAGS) \
		-llagom-core -llagom-coreminimal -llagom-corebasic -llagom-ak -llagom-main

test: build/plan-test
	./build/plan-test

ui-test: $(BIN)
	./tests/ui-test.sh

install: $(BIN)
	install -Dm755 $(BIN) $(DESTDIR)$(PREFIX)/lib/sde/bin/halcyon-setup.real
	sed -e 's|/usr/lib/sde|$(PREFIX)/lib/sde|g' -e 's|/usr/share|$(PREFIX)/share|g' \
		res/halcyon-setup > build/halcyon-setup-wrapper
	install -Dm755 build/halcyon-setup-wrapper $(DESTDIR)$(PREFIX)/bin/halcyon-setup
	install -Dm644 res/halcyon-setup.desktop $(DESTDIR)$(PREFIX)/share/applications/halcyon-setup.desktop
	install -Dm644 res/HalcyonSetup.desktop $(DESTDIR)$(PREFIX)/share/applications/HalcyonSetup.desktop
	install -Dm755 res/HalcyonSetup.desktop $(DESTDIR)/etc/skel/Desktop/HalcyonSetup.desktop
	install -Dm644 res/world-map.png $(DESTDIR)$(PREFIX)/share/halcyon-setup/world-map.png
	for size in 16 24 32 48 64 128 256; do \
		install -Dm644 res/icons/hicolor/$${size}x$${size}/apps/halcyon-setup.png \
			$(DESTDIR)$(PREFIX)/share/icons/hicolor/$${size}x$${size}/apps/halcyon-setup.png; \
	done

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/lib/sde/bin/halcyon-setup.real
	rm -f $(DESTDIR)$(PREFIX)/bin/halcyon-setup
	rm -f $(DESTDIR)$(PREFIX)/share/applications/halcyon-setup.desktop
	rm -f $(DESTDIR)$(PREFIX)/share/applications/HalcyonSetup.desktop
	rm -f $(DESTDIR)/etc/skel/Desktop/HalcyonSetup.desktop
	rm -rf $(DESTDIR)$(PREFIX)/share/halcyon-setup

clean:
	rm -rf build
