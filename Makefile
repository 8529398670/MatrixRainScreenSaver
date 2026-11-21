# Makefile - engine tests, the Linux (X11) build, the macOS .saver, and a
# MinGW cross build of the Windows .scr (used by scripts/windows/build.ps1 via WSL).
#
#   make test            engine tests (SAN=1 adds address/UB sanitizers)
#   make linux           build/matrixrain          (needs libx11-dev libxft-dev)
#   sudo make install-linux                         (xscreensaver hack + idle helper)
#   make deb             build/matrixrain_<ver>_<arch>.deb
#   make macos           build/MatrixRain.saver    (on a Mac, Xcode command line tools)
#   make windows         MatrixRain.scr            (MinGW-w64 cross compiler)

CC      ?= cc
CFLAGS  ?= -O2 -Wall -Wextra
PREFIX  ?= /usr
MINGW   ?= x86_64-w64-mingw32-
ENGINE   = engine/config.c engine/paths.c engine/rain.c engine/render.c
ENGINE_H = engine/config.h engine/paths.h engine/rain.h engine/render.h

ifeq ($(SAN),1)
TESTFLAGS = -g -fsanitize=address,undefined -fno-omit-frame-pointer
endif

.PHONY: all test linux install-linux deb macos macos-zip windows windows-test clean

all: test

build:
	mkdir -p build

# ---- tests ----------------------------------------------------------------
build/engine_test: $(ENGINE) $(ENGINE_H) tests/engine_test.c | build
	$(CC) $(CFLAGS) $(TESTFLAGS) -o $@ $(ENGINE) tests/engine_test.c -lm

test: build/engine_test
	./build/engine_test

# ---- Linux ----------------------------------------------------------------
build/matrixrain: $(ENGINE) $(ENGINE_H) platform/linux/matrixrain.c | build
	$(CC) $(CFLAGS) $$(pkg-config --cflags x11 xft) -o $@ $(ENGINE) platform/linux/matrixrain.c $$(pkg-config --libs x11 xft) -lm

linux: build/matrixrain

install-linux: build/matrixrain
	install -Dm755 build/matrixrain $(DESTDIR)$(PREFIX)/libexec/xscreensaver/matrixrain
	install -d $(DESTDIR)$(PREFIX)/bin
	ln -sf ../libexec/xscreensaver/matrixrain $(DESTDIR)$(PREFIX)/bin/matrixrain
	install -Dm755 platform/linux/matrixrain-idle $(DESTDIR)$(PREFIX)/bin/matrixrain-idle
	install -Dm644 platform/linux/matrixrain.xml $(DESTDIR)$(PREFIX)/share/xscreensaver/config/matrixrain.xml
	install -Dm644 platform/linux/matrixrain-idle.service $(DESTDIR)$(PREFIX)/lib/systemd/user/matrixrain-idle.service
	@echo "Installed. xscreensaver: add 'matrixrain -root' to programs: in ~/.xscreensaver"
	@echo "GNOME:       systemctl --user enable --now matrixrain-idle.service"

deb: build/matrixrain
	sh scripts/linux/build-deb.sh

# ---- macOS ----------------------------------------------------------------
SAVER = build/MatrixRain.saver
$(SAVER)/Contents/MacOS/MatrixRain: $(ENGINE) $(ENGINE_H) platform/macos/MatrixRainView.m platform/macos/Info.plist | build
	mkdir -p $(SAVER)/Contents/MacOS $(SAVER)/Contents/Resources
	clang -fobjc-arc -O2 -Wall -bundle -arch arm64 -arch x86_64 -mmacosx-version-min=11.0 \
		-framework ScreenSaver -framework Cocoa -framework CoreText \
		-o $@ $(ENGINE) platform/macos/MatrixRainView.m
	cp platform/macos/Info.plist $(SAVER)/Contents/Info.plist
	codesign --force --sign - $(SAVER)

macos: $(SAVER)/Contents/MacOS/MatrixRain

macos-zip: macos
	cd build && ditto -c -k --keepParent MatrixRain.saver MatrixRain.saver.zip

# ---- Windows (cross) ------------------------------------------------------
MatrixRain.scr: $(ENGINE) $(ENGINE_H) platform/windows/main.c platform/windows/matrixrain.rc platform/windows/app.manifest | build
	$(MINGW)windres platform/windows/matrixrain.rc -O coff -o build/matrixrain.res.o
	$(MINGW)gcc -O2 -Wall -mwindows -static -o $@ $(ENGINE) platform/windows/main.c build/matrixrain.res.o -lcomctl32 -lgdi32 -luser32 -lshell32

windows: MatrixRain.scr

build/engine_test.exe: $(ENGINE) $(ENGINE_H) tests/engine_test.c | build
	$(MINGW)gcc -O2 -Wall -static -o $@ $(ENGINE) tests/engine_test.c

windows-test: build/engine_test.exe

clean:
	rm -f MatrixRain.scr build/engine_test build/engine_test.exe build/matrixrain build/*.o build/*.res build/*.deb
