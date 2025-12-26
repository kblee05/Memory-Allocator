main: main.o MemoryAllocator.o
	gcc -o main main.o MemoryAllocator.o

main.o: main.c MemoryAllocator.h
	gcc -c main.c

MemoryAllocator.o: MemoryAllocator.c MemoryAllocator.h
	gcc -c MemoryAllocator.c