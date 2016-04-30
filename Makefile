all:
	mkdir -p bin
	cc -std=c99 -Wall src/core.c src/lib/mpc.c -ledit -lm -o bin/slither
