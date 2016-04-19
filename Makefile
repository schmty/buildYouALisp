all:
	cc -std=c99 -Wall parse.c mpc.c -ledit -lm -o bin/slither
