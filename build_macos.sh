#!/bin/sh
# Compila QBLike para macOS (sin probar). Requisitos: brew install sdl2 glew
SRC="src/lexer.c src/compiler.c src/interp.c src/util.c src/using.c src/screen.c src/sound.c src/files.c src/source.c src/ide.c src/help.c src/clip.c src/image.c src/main.c src/doslike/dos.c"
clang -O2 -DDISABLE_SCREEN_FRAME -DDISABLE_SYSTEM_CURSOR -o qblike $SRC `sdl2-config --libs --cflags` -lGLEW -framework OpenGL -lpthread && echo "OK: ./qblike"
