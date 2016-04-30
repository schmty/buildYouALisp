all:
	mkdir -p bin
	cc -std=c99 -Wall src/slither.c src/lib/mpc.c -ledit -lm -o bin/slither
