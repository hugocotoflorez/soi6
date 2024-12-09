all:
	gcc -Wall -Wextra main.c -g -fsanitize=null,address
