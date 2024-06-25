#!/bin/bash

# Get SDL2 compiler and linker flags using sdl2-config
SDL_CFLAGS=$(sdl2-config --cflags)
SDL_LDFLAGS=$(sdl2-config --libs)

# Define the compiler flags
CFLAGS="$SDL_CFLAGS"
LDFLAGS="$SDL_LDFLAGS"

# Parse the command line arguments for custom definitions
for arg in "$@"
do
    case $arg in
        -DPRINT_STATE)
        CFLAGS="$CFLAGS -DPRINT_STATE"
        ;;
        -DDISPLAY_FRAME_TIME)
        CFLAGS="$CFLAGS -DDISPLAY_FRAME_TIME"
        ;;
        -DINIT_EEPROM)
        CFLAGS="$CFLAGS -DINIT_EEPROM"
        ;;
        -Zi)
        CFLAGS="$CFLAGS -g"
        ;;
    esac
done

# Create the bin directory if it doesn't exist
mkdir -p bin

# Compile the source files and link with SDL2
gcc $CFLAGS -o bin/pokeStroller src/walker.c src/lin_main.c src/queue.c $LDFLAGS
