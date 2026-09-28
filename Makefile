# Makefile for Stepbot
# Build the engine with: make
# Clean up with:         make clean
#
# Requires g++ with C++17 support.
# On Windows, use the MSYS2 MinGW x64 terminal.
#
# This Makefile delegates to scr/Makefile

all: stepbot

stepbot:
	cd scr && $(MAKE) all

clean:
	cd scr && $(MAKE) clean

rebuild: clean all

test:
	cd scr && $(MAKE) test

bench:
	cd scr && $(MAKE) bench

bench-multipv:
	cd scr && $(MAKE) bench-multipv

bench-timed:
	cd scr && $(MAKE) bench-timed

run:
	cd scr && $(MAKE) run

# Declare targets that aren't files
.PHONY: all clean rebuild test bench bench-multipv bench-timed run
