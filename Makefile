CFLAGS := -g -Wall

all: vex vexcc
clean:
	rm -f *.o vex vexcc

vex: main.o
	$(CC) $< -lncurses -o $@

vexcc: syn.o
	$(CC) $< -o $@

.PHONY: all clean
