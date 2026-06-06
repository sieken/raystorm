#!/bin/sh

set -e

SRCLIST="raystorm_entry.c"
OUTNAME="raystorm"
SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)

INCLUDES="-I$SCRIPT_DIR/myrlib"

SILENCED_WARNINGS="-Wno-unused-function -Wno-unused-parameter -Wno-unused-variable"
FLAGS="-DDEBUG"
COMMON_COMPILER_FLAGS="$INCLUDES -std=c++17 -Wall -Wextra -Werror $SILENCED_WARNINGS $FLAGS -g"

# COMMON_LINKER_FLAGS="-lX11 -lGL -ldl -lpthread -lrt"
COMMON_LINKER_FLAGS="-lraylib"

TARGET="${1:-all}"

build_game() {
    mkdir -p "$SCRIPT_DIR/build"
    cd "$SCRIPT_DIR/build"

    #
    # Separate game .so + platform layer (cstorm inherited, fix up if using)
    #
    # echo "Waiting for .so..." > lock.tmp
    # echo "Building game code (cstorm.so)..."
    # g++ $COMMON_COMPILER_FLAGS ../src/cstorm.cpp $SHARED -fPIC -shared -o cstorm.so.tmp
    # mv -f cstorm.so.tmp cstorm.so
    # rm -f lock.tmp
    # echo "Building platform layer (linux64_cstorm)..."
    # g++ $COMMON_COMPILER_FLAGS ../src/linux64_cstorm.cpp $SHARED -o linux64_cstorm $COMMON_LINKER_FLAGS -lXrandr -Wl,-export-dynamic

    g++ $COMMON_COMPILER_FLAGS "$SCRIPT_DIR/$SRCLIST" -o $OUTNAME $COMMON_LINKER_FLAGS

    cd "$SCRIPT_DIR"
}

run_game() {
    env --chdir=runtime ../build/$OUTNAME
}

case "$TARGET" in
    all)
        build_game
        ;;
    game)
        build_game
        ;;
    run)
        run_game
        ;;
    *)
        echo "Usage: $0 [all|game|assets|run]" >&2
        exit 1
        ;;
esac
