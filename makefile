all: main bench_my bench_std libmymalloc.a

libmymalloc: MemoryAllocator.o MemoryAllocator.h
	ar rcs libmymalloc.a MemoryAllocator.o

main: main.o MemoryAllocator.o
	gcc -o main main.o MemoryAllocator.o

bench_my: benchmark.c MemoryAllocator.o
	gcc -o bench_my benchmark.c MemoryAllocator.o

bench_std: benchmark.c MemoryAllocator.o
	gcc -DUSE_STD_MALLOC -o bench_std benchmark.c MemoryAllocator.o

main.o: main.c MemoryAllocator.h
	gcc -c main.c

MemoryAllocator.o: MemoryAllocator.c MemoryAllocator.h
	gcc -c MemoryAllocator.c