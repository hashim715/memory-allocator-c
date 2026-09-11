#include <sys/mman.h>
#include <unistd.h>
#include <stdio.h>

typedef struct block_meta {
    size_t size;
    int free;
    struct block_meta *next;
} __attribute__((aligned(16))) block_meta;

// header -> user pointer (move FORWARD past the header)
void* block_to_ptr(block_meta *block) {
    return (void*)(block + 1);
};

// user pointer -> header (move BACKWARD to find the header)
block_meta* ptr_to_block(void* user_ptr) {
    return (block_meta*)user_ptr - 1;
};

// a real, reusable helper: mmap a chunk and set up its header properly
block_meta* make_block(int user_size) {
    size_t size_to_request = sizeof(block_meta) + user_size;

    void* chunk = mmap(NULL, size_to_request, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

    if (chunk == MAP_FAILED) {
        perror("mmap failed");
        return NULL;
    };

    block_meta* header = (block_meta*)chunk;

    header->size = user_size;
    header->free = 0;
    header->next = NULL;

    return header;
};

int main(int argc, char** argv) {
    // allocate a FEW blocks, as the Phase 1 success test asks for
    block_meta *h1 = make_block(20);
    block_meta *h2 = make_block(100);
    block_meta *h3 = make_block(7);
 
    void *p1 = block_to_ptr(h1);
    void *p2 = block_to_ptr(h2);
    void *p3 = block_to_ptr(h3);

    printf("Requested 20  -> header->size = %zu  %s\n", h1->size, h1->size == 20  ? "(correct)" : "(WRONG)");
    printf("Requested 100 -> header->size = %zu  %s\n", h2->size, h2->size == 100 ? "(correct)" : "(WRONG)");
    printf("Requested 7   -> header->size = %zu  %s\n", h3->size, h3->size == 7   ? "(correct)" : "(WRONG)");

    printf("sizeof(block_meta) now = %zu\n", sizeof(block_meta));
    printf("is multiple of 16?     = %s\n", (sizeof(block_meta) % 16 == 0) ? "yes" : "no");

    // prove ptr_to_block correctly reverses block_to_ptr, using ONLY the user pointer
    block_meta *recovered = ptr_to_block(p1);
    printf("\nRecovered header from p1 alone -> size = %zu (should be 20)\n", recovered->size);
    printf("recovered == h1? %s\n", (recovered == h1) ? "yes" : "no");

    block_meta *headers[3] = { h1, h2, h3 };
    void *ptrs[3] = { p1, p2, p3 };

    for (int i = 0; i < 3; i++) {
        block_meta *header = headers[i];

        void *user_ptr = ptrs[i];

        size_t bytes_between = (char*)user_ptr - (char*)header;

        printf("\nblock %d:\n", i + 1);

        printf("  mmap chunk starts at:  %p  (same address as header)\n", (void*)header);

        printf("  user data pointer at:  %p\n", user_ptr);

        printf("  bytes between them:    %zu  %s\n", bytes_between,
               bytes_between == sizeof(block_meta) ? "(== sizeof(block_meta), correct)" : "(WRONG)");
        printf("  requested size:        %zu\n", header->size);
    };
};