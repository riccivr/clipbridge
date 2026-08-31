# clipbridge version
VERSION = 1.1.0

# Customize below to fit your system

PREFIX = /usr/local
MANPREFIX = $(PREFIX)/share/man

CPPFLAGS = -D_DEFAULT_SOURCE -D_BSD_SOURCE -D_POSIX_C_SOURCE=200809L -D_DARWIN_C_SOURCE -DVERSION=\"$(VERSION)\" -I../unipaste
CFLAGS   = -std=c99 -pedantic -Wall -Wextra -Os $(CPPFLAGS)
LDFLAGS  = -s

CC = cc
