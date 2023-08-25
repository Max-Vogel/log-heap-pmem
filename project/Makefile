CC = gcc
SOURCE = main.c
OUTPUT = log_pmem
CFLAGS = -Wall `pkg-config --cflags glib-2.0`
LIBS = -lpmem2 -lpthread `pkg-config --libs glib-2.0`

# $(OUTPUT): main.c log_pmem.c log_pmem.h log.c log.h hash_table.c hash_table.h cleaner.c cleaner.h #log_pmem_types.h
# 		$(CC) main.c log_pmem.c log.c hash_table.c cleaner.c -o $(OUTPUT) $(CFLAGS) $(LIBS)

$(OUTPUT): $(SOURCE) log_pmem.o log.o hash_table.o cleaner.o #log_pmem_types.h
	$(CC) $(SOURCE) log_pmem.o log.o hash_table.o cleaner.o -g -o $(OUTPUT) $(CFLAGS) $(LIBS)

log_pmem.o: log_pmem.c
	$(CC) -c log_pmem.c -o log_pmem.o $(CFLAGS) $(LIBS)

log.o: log.c
	$(CC) -c log.c -o log.o $(CFLAGS) $(LIBS)

cleaner.o: cleaner.c
	$(CC) -c cleaner.c -o cleaner.o $(CFLAGS) $(LIBS)

hash_table.o: hash_table.c
	$(CC) -c hash_table.c -o hash_table.o $(CFLAGS) $(LIBS)

clean:
	rm *.o $(OUTPUT)
