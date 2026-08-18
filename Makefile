# clipbridge - universal clipboard bridge & background synchronization daemon
# See LICENSE file for copyright and license details.

include config.mk

UNIPASTE_SRC = ../unipaste/parser.c ../unipaste/table.c ../unipaste/entity.c ../unipaste/strbuf.c
UNIPASTE_OBJ = parser.o table.o entity.o strbuf.o

SRC = clipbridge.c platform_posix.c platform_win32.c
OBJ = clipbridge.o platform_posix.o platform_win32.o $(UNIPASTE_OBJ)

all: clipbridge

.c.o:
	$(CC) -c $(CFLAGS) $<

parser.o: ../unipaste/parser.c
	$(CC) -c $(CFLAGS) $< -o $@

table.o: ../unipaste/table.c
	$(CC) -c $(CFLAGS) $< -o $@

entity.o: ../unipaste/entity.c
	$(CC) -c $(CFLAGS) $< -o $@

strbuf.o: ../unipaste/strbuf.c
	$(CC) -c $(CFLAGS) $< -o $@

clipbridge: $(OBJ)
	$(CC) -o $@ $(OBJ) $(LDFLAGS)

clean:
	rm -f clipbridge $(OBJ) clipbridge-$(VERSION).tar.gz

install: all
	mkdir -p $(DESTDIR)$(PREFIX)/bin
	cp -f clipbridge $(DESTDIR)$(PREFIX)/bin
	chmod 755 $(DESTDIR)$(PREFIX)/bin/clipbridge

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/clipbridge

test: all
	@echo "Running clipbridge smoke tests..."
	./clipbridge -v
	./clipbridge -h 2>&1 | grep -q "usage:"
	@echo "[PASS] clipbridge CLI options verified"

.PHONY: all clean install uninstall test
