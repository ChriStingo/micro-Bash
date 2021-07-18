all:
	rm -rf ubash
	gcc -std=c11 -Wall -pedantic -Werror -ggdb code/*.c -o ubash

clean:
	rm -rf ubash