CC = gcc
CFLAGS = -Wall `pkg-config --cflags glib-2.0`
LIBS = -lpmem2 -lpthread `pkg-config --libs glib-2.0`

SOURCE = main.c
OUTPUT = main

TEST_SOURCE = test.c
TEST_OUTPUT = test

TEST_BIG_SOURCE = test_big.c
TEST_BIG_OUTPUT = test_big

LIB_STATIC_OUTPUT = liblog_pmem.a

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

static_lib: log_pmem.o log.o hash_table.o cleaner.o
	ar rc $(LIB_STATIC_OUTPUT) log_pmem.o log.o hash_table.o cleaner.o

test: $(TEST_SOURCE) log_pmem.o log.o hash_table.o cleaner.o log_pmem_types.h
	$(CC) $(TEST_SOURCE) log_pmem.o log.o hash_table.o cleaner.o -g -o $(TEST_OUTPUT) $(CFLAGS) $(LIBS)

test_big: $(TEST_BIG_SOURCE) log_pmem.o log.o hash_table.o cleaner.o log_pmem_types.h
	$(CC) $(TEST_BIG_SOURCE) log_pmem.o log.o hash_table.o cleaner.o -g -o $(TEST_BIG_OUTPUT) $(CFLAGS) $(LIBS)

clean:
	rm *.o $(OUTPUT) $(TEST_OUTPUT) $(TEST_BIG_OUTPUT) $(LIB_STATIC_OUTPUT)
