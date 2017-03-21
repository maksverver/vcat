CFLAGS=-g -O2 -Wall -Wextra -std=c99 -D_FILE_OFFSET_BITS=64

all: vcat

install: vcat
	install -s "$<" "${HOME}/bin/"

clean:
	rm -f vcat

.PHONY: all install clean
