#include <sys/mman.h>
#include <unistd.h>
#include <stdio.h>

int main(int argc, char** argv) {
    size_t size = 4096; // 4KB (usually one page)

    void* ptr = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

    if (ptr == MAP_FAILED) {
        perror("mmap failed");
        return 1;
    };

    // Use the memory like a normal array
    printf("this is a pointer buddy %p\n",ptr);
};