CFLAGS=-g -O2 -Wall -Wextra -std=c99 -D_FILE_OFFSET_BITS=64

all: vcat

install: vcat vcp
	install -s vcat "${HOME}/bin/"
	install vcp "${HOME}/bin/"

test: vcat
	vcat /.xyzzy >/dev/null

clean:
	rm -f vcat

.PHONY: all install clean
