LDLIBS := -lncurses
CFLAGS += -g -Wall

all: vex
clean:
	rm -f *.o vex

vex: main.o
	$(CC) $< $(LDLIBS) -o $@

install: vex
	install vex $(DESTDIR)$(INSTALLDIR)/vex

VERSION := 1.0
release:
	CFLAGS=-D'VERSION=\"$(VERSION)\"' make clean vex
	./vex -v

.PHONY: all clean install release
