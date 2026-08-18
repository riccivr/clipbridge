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
LDFLAGS += -framework AppKit -framework Foundation
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
	rm -f clipbridge $(OBJ) plugin.o platform_macos.o platform_posix.o platform_win32.o clipbridge-$(VERSION).tar.gz

dist: clean
	mkdir -p clipbridge-$(VERSION)
	cp -R LICENSE Makefile README.md config.mk clipbridge.1 arg.h clipbridge.h clipbridge.c platform_posix.c platform_win32.c platform_macos.m clipbridge-$(VERSION)
	tar -cf clipbridge-$(VERSION).tar clipbridge-$(VERSION)
	gzip clipbridge-$(VERSION).tar
	rm -rf clipbridge-$(VERSION)

install: all
	mkdir -p $(DESTDIR)$(PREFIX)/bin
	cp -f clipbridge $(DESTDIR)$(PREFIX)/bin
	chmod 755 $(DESTDIR)$(PREFIX)/bin/clipbridge
	mkdir -p $(DESTDIR)$(MANPREFIX)/man1
	sed "s/VERSION/$(VERSION)/g" < clipbridge.1 > $(DESTDIR)$(MANPREFIX)/man1/clipbridge.1
	chmod 644 $(DESTDIR)$(MANPREFIX)/man1/clipbridge.1

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/clipbridge
	rm -f $(DESTDIR)$(MANPREFIX)/man1/clipbridge.1

test: all
	@echo "Running clipbridge smoke tests..."
	./clipbridge -v
	./clipbridge -h 2>&1 | grep -q "usage:"
	@echo "[PASS] clipbridge CLI options verified"

.PHONY: all clean dist install uninstall test
