CC=g++
CFLAGS=-std=c++11 -c -g -Wall -fgnu-tm -O2 `pkg-config --cflags glib-2.0`
INCLUDES=-I../
SOURCES=$(wildcard *.cc)
OBJECTS=$(SOURCES:.cc=.o)

all: $(SOURCES) $(OBJECTS)

.cc.o:
	$(CC) $(CFLAGS) $(INCLUDES) $< -o $@

clean:
	$(RM) $(OBJECTS)
