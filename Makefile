all: thor

thor:
	$(CC) thor.c -o thor -Wall -Wextra -pedantic -std=c99

clean:
	rm -f thor

install: thor
	cp thor /bin/thor
