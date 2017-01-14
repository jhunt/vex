LDLIBS := -lncurses
CFLAGS := -g

main: main.o
clean:
	rm -f *.o main

