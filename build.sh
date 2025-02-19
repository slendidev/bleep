#!/bin/sh

[ -e libvinput.so ] || curl -LO https://github.com/slendidev/libvinput/releases/download/v1.2.3/libvinput.so
[ -e libvinput.h ] || curl -LO https://github.com/slendidev/libvinput/releases/download/v1.2.3/libvinput.h

cc $(pkg-config --libs --cflags libpipewire-0.3) -lX11 -lXtst $(pkg-config --cflags --libs libevdev) -lxdo -lm -L. -lvinput -o bleep bleep.c -g

