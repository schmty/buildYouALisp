all:
	mkdir -p bin
	cc -std=c99 -Wall slither.c ext/mpc.c -ledit -lm -o bin/slither
