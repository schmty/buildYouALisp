all:
	cc -std=c99 -Wall eval.c mpc.c -ledit -lm -o bin/slither
