CC = gcc
CFLAGS = -Wall -O2
LDFLAGS = -lpthread
AR = ar
ARFLAGS = rcs

ALL_TARGETS = main bench_my bench_std libmymalloc.a

all: $(ALL_TARGETS)

libmymalloc.a: MemoryAllocator.o
	$(AR) $(ARFLAGS) libmymalloc.a MemoryAllocator.o

main: main.o MemoryAllocator.o
	$(CC) $(CFLAGS) -o main main.o MemoryAllocator.o $(LDFLAGS)

bench_my: benchmark.o MemoryAllocator.o
	$(CC) $(CFLAGS) -o bench_my benchmark.o MemoryAllocator.o $(LDFLAGS)

bench_std: benchmark.o MemoryAllocator.o
	$(CC) $(CFLAGS) -DUSE_STD_MALLOC -o bench_std benchmark.o MemoryAllocator.o $(LDFLAGS)

main.o: main.c MemoryAllocator.h
	$(CC) $(CFLAGS) -c main.c

benchmark.o: benchmark.c MemoryAllocator.h
	$(CC) $(CFLAGS) -c benchmark.c

MemoryAllocator.o: MemoryAllocator.c MemoryAllocator.h
	$(CC) $(CFLAGS) -c MemoryAllocator.c

clean:
	rm -f $(ALL_TARGETS) *.o