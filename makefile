all:
	gcc -Wall -Wextra main.c -o programa -fsanitize=null,address
