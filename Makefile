all:
	cc -std=c99 -Wall parsing.c mpc.c -ledit -lm -o bin/parsing
