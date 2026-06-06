.PHONY: all game assets run

all:
	./build.sh all

game:
	./build.sh game

run: game
	./build.sh run
