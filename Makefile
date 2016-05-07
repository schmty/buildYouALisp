build:
	mkdir -p bin
	cc -std=c99 -Wall src/core.c src/lib/mpc.c -ledit -lm -o bin/slither

install:
	cp -r lib/slither /usr/local/lib
	cc -std=c99 -Wall src/core.c src/lib/mpc.c -ledit -lm -o /usr/local/bin/slither
