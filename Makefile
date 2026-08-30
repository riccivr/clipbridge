# clipbridge - universal clipboard bridge & background synchronization daemon
# See LICENSE file for copyright and license details.

include config.mk

UNAME_S := $(shell uname -s)

PLUGIN_SRC = ../unipaste/plugin_none.c
ifeq ($(SANITIZE),builtin)
PLUGIN_SRC = ../unipaste/plugin_builtin.c
CFLAGS += -DSANITIZE_BUILTIN
endif

UNIPASTE_SRC = ../unipaste/parser.c ../unipaste/table.c ../unipaste/entity.c ../unipaste/strbuf.c $(PLUGIN_SRC)
UNIPASTE_OBJ = parser.o table.o entity.o strbuf.o plugin.o

ifeq ($(UNAME_S),Darwin)
PLATFORM_SRC = platform_macos.m
PLATFORM_OBJ = platform_macos.o
LDFLAGS = -Wl,-dead_strip -framework AppKit -framework Foundation
else
PLATFORM_SRC = platform_posix.c platform_win32.c
PLATFORM_OBJ = platform_posix.o platform_win32.o
endif

SRC = clipbridge.c $(PLATFORM_SRC)
OBJ = clipbridge.o $(PLATFORM_OBJ) $(UNIPASTE_OBJ)

all: clipbridge

.c.o:
	$(CC) -c $(CFLAGS) $<

.m.o:
	$(CC) -c $(CFLAGS) $<

parser.o: ../unipaste/parser.c
	$(CC) -c $(CFLAGS) $< -o $@

table.o: ../unipaste/table.c
	$(CC) -c $(CFLAGS) $< -o $@

entity.o: ../unipaste/entity.c
	$(CC) -c $(CFLAGS) $< -o $@

strbuf.o: ../unipaste/strbuf.c
	$(CC) -c $(CFLAGS) $< -o $@

plugin.o: $(PLUGIN_SRC)
	$(CC) -c $(CFLAGS) $< -o $@

clipbridge: $(OBJ)
	$(CC) -o $@ $(OBJ) $(LDFLAGS)

clean:
	rm -f clipbridge $(OBJ) plugin.o platform_macos.o platform_posix.o platform_win32.o clipbridge_res.o installer_res.o clipbridge.exe clipbridge-portable.exe clipbridge-setup.exe clipbridge-$(VERSION).tar.gz

dist: clean
	mkdir -p clipbridge-$(VERSION)/scripts clipbridge-$(VERSION)/packaging clipbridge-$(VERSION)/assets
	cp -R LICENSE Makefile README.md config.mk Info.plist clipbridge.rc installer.rc clipbridge.1 arg.h clipbridge.h clipbridge.c installer_win32.c platform_posix.c platform_win32.c platform_macos.m scripts packaging assets clipbridge-$(VERSION)
	tar -cf clipbridge-$(VERSION).tar clipbridge-$(VERSION)
	gzip clipbridge-$(VERSION).tar
	rm -rf clipbridge-$(VERSION)

deb: all
	@T=$$(mktemp -d); \
	mkdir -p $$T/DEBIAN $$T/usr/bin $$T/usr/share/man/man1 $$T/usr/share/applications $$T/usr/share/icons/hicolor/scalable/apps $$T/usr/share/pixmaps; \
	cp clipbridge $$T/usr/bin/; \
	sed "s/VERSION/$(VERSION)/g" < clipbridge.1 > $$T/usr/share/man/man1/clipbridge.1; \
	cp packaging/clipbridge.desktop $$T/usr/share/applications/; \
	cp assets/clipbridge.svg $$T/usr/share/icons/hicolor/scalable/apps/; \
	cp assets/clipbridge.png $$T/usr/share/pixmaps/; \
	chmod 755 $$T/usr/bin/clipbridge; \
	chmod 644 $$T/usr/share/man/man1/clipbridge.1 $$T/usr/share/applications/clipbridge.desktop $$T/usr/share/icons/hicolor/scalable/apps/clipbridge.svg $$T/usr/share/pixmaps/clipbridge.png; \
	printf "Package: clipbridge\nVersion: $(VERSION)\nSection: utils\nPriority: optional\nArchitecture: amd64\nMaintainer: Ricardo Veronese Ricci <riccivr@users.noreply.github.com>\nDescription: Universal clipboard bridge and background daemon powered by unipaste\n" > $$T/DEBIAN/control; \
	chmod 755 $$T/DEBIAN; \
	dpkg-deb --root-owner-group --build $$T clipbridge_$(VERSION)_amd64.deb; \
	rm -rf $$T; \
	echo "Built clipbridge_$(VERSION)_amd64.deb"

CC_WIN32 ?= x86_64-w64-mingw32-gcc
WINDRES  ?= x86_64-w64-mingw32-windres
exe:
	$(WINDRES) clipbridge.rc -O coff -o clipbridge_res.o
	$(CC_WIN32) $(CFLAGS) -D_WIN32 -mwindows clipbridge.c platform_win32.c clipbridge_res.o $(UNIPASTE_SRC) -o clipbridge.exe -s -luser32 -lshell32 -ladvapi32
	cp clipbridge.exe clipbridge-portable.exe
	$(WINDRES) installer.rc -O coff -o installer_res.o
	$(CC_WIN32) $(CFLAGS) -D_WIN32 -mwindows installer_win32.c installer_res.o -o clipbridge-setup.exe -s -luser32 -lshell32 -ladvapi32 -lole32 -luuid
	@echo "Built clipbridge.exe, clipbridge-portable.exe, and clipbridge-setup.exe"

dmg: all
	sh scripts/build_dmg.sh

install: all
	mkdir -p $(DESTDIR)$(PREFIX)/bin
	cp -f clipbridge $(DESTDIR)$(PREFIX)/bin
	chmod 755 $(DESTDIR)$(PREFIX)/bin/clipbridge
	mkdir -p $(DESTDIR)$(MANPREFIX)/man1
	sed "s/VERSION/$(VERSION)/g" < clipbridge.1 > $(DESTDIR)$(MANPREFIX)/man1/clipbridge.1
	chmod 644 $(DESTDIR)$(MANPREFIX)/man1/clipbridge.1
	mkdir -p $(DESTDIR)$(PREFIX)/share/applications
	cp -f packaging/clipbridge.desktop $(DESTDIR)$(PREFIX)/share/applications/clipbridge.desktop
	chmod 644 $(DESTDIR)$(PREFIX)/share/applications/clipbridge.desktop
	mkdir -p $(DESTDIR)$(PREFIX)/share/icons/hicolor/scalable/apps
	cp -f assets/clipbridge.svg $(DESTDIR)$(PREFIX)/share/icons/hicolor/scalable/apps/clipbridge.svg
	chmod 644 $(DESTDIR)$(PREFIX)/share/icons/hicolor/scalable/apps/clipbridge.svg
	mkdir -p $(DESTDIR)$(PREFIX)/share/pixmaps
	cp -f assets/clipbridge.png $(DESTDIR)$(PREFIX)/share/pixmaps/clipbridge.png
	chmod 644 $(DESTDIR)$(PREFIX)/share/pixmaps/clipbridge.png

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/clipbridge
	rm -f $(DESTDIR)$(MANPREFIX)/man1/clipbridge.1
	rm -f $(DESTDIR)$(PREFIX)/share/applications/clipbridge.desktop
	rm -f $(DESTDIR)$(PREFIX)/share/icons/hicolor/scalable/apps/clipbridge.svg
	rm -f $(DESTDIR)$(PREFIX)/share/pixmaps/clipbridge.png

test: all
	@echo "Running clipbridge smoke tests..."
	./clipbridge -v
	./clipbridge -h 2>&1 | grep -q "usage:"
	@echo "[PASS] clipbridge CLI options verified"

.PHONY: all clean dist deb exe dmg install uninstall test
