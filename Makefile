build:
	mkdir -p bin
	cc -std=c99 -Wall src/core.c src/lib/mpc.c -ledit -lm -o bin/slither

install:
	cc -std=c99 -Wall src/core.c src/lib/mpc.c -ledit -lm -o /usr/local/bin/slither
