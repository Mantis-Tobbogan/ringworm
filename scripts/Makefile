# Helper binaries: virtmouse (unattended acceptance testing) and render-demo
# (regenerates the README media from the daemon's real ring math).

WAYLAND := $(shell pkg-config --cflags wayland-client) -I.
LDLIBS := $(shell pkg-config --libs wayland-client)

CFLAGS ?= -O2
CFLAGS += -std=c11 -Wall -Wextra -Werror

PROTO := ../protocol
VP := $(PROTO)/wlr-virtual-pointer-unstable-v1.xml

all: virtmouse render-demo

virtual-pointer.h: $(VP)
	wayland-scanner client-header $< $@

virtual-pointer.c: $(VP)
	wayland-scanner private-code $< $@

virtmouse: virtmouse.c virtual-pointer.c virtual-pointer.h
	$(CC) $(CFLAGS) $(WAYLAND) -o $@ virtmouse.c virtual-pointer.c $(LDLIBS)

render-demo: render-demo.c ../daemon/ring.c ../daemon/ring.h
	$(CC) $(CFLAGS) -I../daemon -o $@ render-demo.c ../daemon/ring.c -lm

demo: render-demo
	./make-demo-gif.sh

clean:
	rm -f virtmouse render-demo virtual-pointer.h virtual-pointer.c

.PHONY: all clean demo
