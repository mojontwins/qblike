#!/bin/sh
# Compila QBLike para Linux.
# Requisitos (Debian/Ubuntu):  sudo apt-get install build-essential libsdl2-dev libglew-dev
# Fedora:                       sudo dnf install gcc SDL2-devel glew-devel
CORE="src/lexer.c src/compiler.c src/interp.c src/util.c src/using.c src/screen.c src/sound.c src/files.c src/source.c src/image.c src/exepack.c src/doslike/dos.c"
# qblike: editor + interpreter.  qbrun: run-time module for File > Make EXE (no editor)
FLAGS="-O2 -DDISABLE_SCREEN_FRAME -DDISABLE_SYSTEM_CURSOR"
LIBS="`sdl2-config --libs --cflags` -lGLEW -lGL -lm -lpthread"
gcc $FLAGS -o qblike $CORE src/ide.c src/help.c src/clip.c src/beautify.c src/main.c $LIBS && echo "OK: ./qblike" &&
gcc $FLAGS -o qbrun $CORE src/runtime.c $LIBS && echo "OK: ./qbrun"
