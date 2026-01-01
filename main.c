#include <stdio.h>
#include "MemoryAllocator.h"
#include <string.h>

int main(){
    char *test = my_malloc(1024 * sizeof(char));
    char *dup;
    test = "hello world";
    dup = my_strdup(test);

    printf("%ld\n%s\n", strlen(dup), dup);

    return 0;
}