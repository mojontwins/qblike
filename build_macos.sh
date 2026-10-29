#!/bin/sh
# Compila QBLike para macOS (sin probar). Requisitos: brew install sdl2 glew
CORE="src/lexer.c src/compiler.c src/interp.c src/util.c src/using.c src/screen.c src/sound.c src/files.c src/source.c src/image.c src/exepack.c src/doslike/dos.c"
FLAGS="-O2 -DDISABLE_SCREEN_FRAME -DDISABLE_SYSTEM_CURSOR"
LIBS="`sdl2-config --libs --cflags` -lGLEW -framework OpenGL -lpthread"
clang $FLAGS -o qblike $CORE src/ide.c src/help.c src/clip.c src/beautify.c src/main.c $LIBS && echo "OK: ./qblike" &&
clang $FLAGS -o qbrun $CORE src/runtime.c $LIBS && echo "OK: ./qbrun"
