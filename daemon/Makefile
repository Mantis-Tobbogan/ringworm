# ringworm daemon — pure C11 + libwayland-client (see docs/adr/0004)
#
#   make            build ./ringworm
#   make install    install to $(PREFIX)/bin (default ~/.local/bin)
#   make check      rebuild from scratch with -Werror and strict warnings
#   make clean

VERSION ?= 0.2.0
PREFIX ?= $(HOME)/.local
BINDIR ?= $(PREFIX)/bin
CC ?= cc

WAYLAND := $(shell pkg-config --cflags wayland-client) -I.
LDLIBS := $(shell pkg-config --libs wayland-client) -lm

CFLAGS ?= -O2
CFLAGS += -std=c11 -Wall -Wextra -Werror
CPPFLAGS += -DRINGWORM_VERSION='"$(VERSION)"'

PROTO := ../protocol

all: ringworm

layer-shell.h: $(PROTO)/wlr-layer-shell-unstable-v1.xml
	wayland-scanner client-header $< $@

layer-shell.c: $(PROTO)/wlr-layer-shell-unstable-v1.xml
	wayland-scanner private-code $< $@

xdg-shell.c: $(PROTO)/xdg-shell.xml
	wayland-scanner private-code $< $@

# xdg-shell.c is linked only for the wl_interface symbols referenced by
# layer-shell's get_popup argument table; none of its code paths run.
ringworm: src/main.c ring.c ring.h layer-shell.c layer-shell.h xdg-shell.c
	$(CC) $(CFLAGS) $(CPPFLAGS) $(WAYLAND) -o $@ \
		src/main.c ring.c layer-shell.c xdg-shell.c $(LDLIBS)

install: ringworm
	install -Dm755 ringworm $(DESTDIR)$(BINDIR)/ringworm

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/ringworm

check:
	$(MAKE) clean
	$(MAKE) all

clean:
	rm -f ringworm layer-shell.h layer-shell.c xdg-shell.c

.PHONY: all install uninstall check clean
