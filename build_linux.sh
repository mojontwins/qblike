#!/bin/sh
# Compila QBLike para Linux.
# Requisitos (Debian/Ubuntu):  sudo apt-get install build-essential libsdl2-dev libglew-dev
# Fedora:                       sudo dnf install gcc SDL2-devel glew-devel
SRC="src/lexer.c src/compiler.c src/interp.c src/util.c src/using.c src/screen.c src/sound.c src/files.c src/source.c src/ide.c src/help.c src/clip.c src/image.c src/main.c src/doslike/dos.c"
gcc -O2 -DDISABLE_SCREEN_FRAME -o qblike $SRC `sdl2-config --libs --cflags` -lGLEW -lGL -lm -lpthread && echo "OK: ./qblike"
