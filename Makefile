$(CC) = gcc
all:
	$(CC) -o myshell shell2.c

clean:
	rm -f *.o myshell
