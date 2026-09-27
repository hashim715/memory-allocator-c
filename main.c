#include <sys/mman.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include "allocator.h"

int main(int argc, char** argv) {
    (void)argc;(void)argv;

    // Phase 1: my_malloc allocates memory
    void *p1 = my_malloc(20);
    void *p2 = my_malloc(100);
    void *p3 = my_malloc(7);
    (void)p1; (void)p2; (void)p3;
    print_list();

    // Phase 2: find_free_block / my_free reuse a freed block
    void *a = my_malloc(20);
    my_free(a);
    void *b = my_malloc(10);
    printf("\na==b? %s\n", (a == b) ? "YES (reuse works)" : "NO (bug)");

    void *m1 = my_malloc(1000);
    void *m2 = my_malloc(1000);
    void *m3 = my_malloc(5000);
    void *m4 = my_malloc(1000);
    void *m5 = my_malloc(1000);
    my_free(m3);
    void *reused = my_malloc(30);
    printf("middle-block reuse: reused==m3? %s\n", (reused == m3) ? "YES" : "NO (bug)");
    (void)m1; (void)m2; (void)m4; (void)m5;

    // Phase 3: split_block carves the reused block into used + leftover
    print_list();

    // Phase 4: coalesce merges adjacent free blocks back together.
    void *q1 = my_malloc(20000);
    void *q2 = my_malloc(20000);
    void *q3 = my_malloc(20000);
    (void)q1; (void)q2; (void)q3;

    my_free(q1);
    my_free(q2);
    my_free(q3);

    print_list();

    // Phase 5: my_calloc zero-initializes memory and rejects bad input (overflow, size=0).
    // my_realloc: NULL acts like my_malloc, new_size=0 acts like my_free. Shrinking (or
    // requesting the same size) reuses split_block() to return the same block, resized
    // in place. Growing tries to absorb an adjacent free neighbor first, and only
    // falls back to a fresh my_malloc + copy + my_free if no adjacent space fits.

   char *z = (unsigned char*)my_calloc(10, sizeof(char));
    int all_zero = 1;
    for (size_t i = 0; i < 10; i++) {
        if (z[i] != 0) {
            all_zero = 0;
            break;
        };
    };
    printf("\nmy_calloc zero-initialized? %s\n", all_zero ? "YES" : "NO (bug)");

    void *overflow = my_calloc((size_t)-1, 2);
    printf("my_calloc overflow rejected? %s\n", (overflow == NULL) ? "YES" : "NO (bug)");

    void *zero_size = my_calloc(5, 0);
    printf("my_calloc size=0 rejected? %s\n", (zero_size == NULL) ? "YES" : "NO (bug)");

    void *r_null = my_realloc(NULL, 40);
    printf("\nmy_realloc(NULL, size) behaves like my_malloc? %s\n", (r_null != NULL) ? "YES" : "NO (bug)");

    void *r_freeme = my_malloc(40);
    void *r_freed = my_realloc(r_freeme, 0);
    void *r_reuse_check = my_malloc(40);
    printf("my_realloc(ptr, 0) frees block (reused after)? %s\n", (r_freed == NULL && r_reuse_check == r_freeme) ? "YES" : "NO (bug)");

    void *r_big = my_malloc(500);
    void *r_shrunk = my_realloc(r_big, 50);
    printf("my_realloc shrink keeps same pointer? %s\n", (r_shrunk == r_big) ? "YES" : "NO (bug)");

    void *r_first = my_malloc(200);
    void *r_second = my_malloc(200);
    my_free(r_second);
    void *r_grown = my_realloc(r_first, 300);
    printf("my_realloc grow merges adjacent free neighbor (same pointer)? %s\n", (r_grown == r_first) ? "YES" : "NO (bug)");

    void *r_data = my_malloc(20);
    memcpy(r_data, "hello-realloc-data", 19);
    void *r_moved = my_realloc(r_data, 50000);
    int r_data_preserved = (r_moved != NULL) && (memcmp(r_moved, "hello-realloc-data", 19) == 0);
    printf("my_realloc fallback moves and preserves data? %s\n", r_data_preserved ? "YES" : "NO (bug)");

    print_list();

    // Phase 6: requests >= MMAP_THRESHOLD get their own mmap'd block (is_mmapped=1),
    // kept out of the shared free list, and my_free() munmaps them directly.
    void *mmap_big_block = my_malloc(128*1024);
    block_meta *is_mmapped_block = ptr_to_block(mmap_big_block);
    printf("is this is_mmapped block? %s\n", (is_mmapped_block->is_mmapped) ? "YES" : "NO (bug)");

    void *below_threshold = my_malloc(128 * 1024 - 1);
    block_meta *below_block = ptr_to_block(below_threshold);
    printf("below-threshold block NOT is_mmapped? %s\n", (!below_block->is_mmapped) ? "YES" : "NO (bug)");

    void *mmap_to_free = my_malloc(200000);
    my_free(mmap_to_free);
    void *after_free_small = my_malloc(20);
    printf("mmap'd block munmapped, not reused via free list? %s\n", (after_free_small != mmap_to_free) ? "YES" : "NO (bug)");

    void *mmap_grow = my_malloc(200000);
    memset(mmap_grow, 0xAB, 200000);
    void *mmap_grown = my_realloc(mmap_grow, 400000);
    int mmap_grow_preserved = (mmap_grown != mmap_grow) && (((unsigned char*)mmap_grown)[0] == 0xAB) && (((unsigned char*)mmap_grown)[199999] == 0xAB);
    printf("my_realloc grows mmap'd block via move, data preserved? %s\n", mmap_grow_preserved ? "YES" : "NO (bug)");

    void *mmap_shrink = my_malloc(200000);
    void *mmap_shrunk = my_realloc(mmap_shrink, 150000);
    printf("my_realloc shrinks mmap'd block in place (same pointer)? %s\n", (mmap_shrunk == mmap_shrink) ? "YES" : "NO (bug)");

    print_list();

    void *small = my_malloc(32);    // becomes the free "previous"
    void *big   = my_malloc(256);   // the block we'll grow
    void *guard = my_malloc(32);    // stops "next" from being used
    memset(big, 'X', 256);
    my_free(small);
    void *grown = my_realloc(big, 280);
    printf("realloc merge-previous preserves data? %s\n",
        (grown == small && ((unsigned char*)grown)[0] == 'X' && ((unsigned char*)grown)[255] == 'X') ? "YES" : "NO (bug)");
    (void)guard;

    print_list();

    // Phase 7: first-fit vs best-fit
    size_t req = 64;

    set_strategy(FIRST_FIT);
    block_meta *ff = find_free_block(req);
    block_meta *first = NULL;
    for (block_meta *c = global_head; c != NULL; c = c->next)
        if (c->free == 1 && c->size >= req) { first = c; break; };
    printf("\nfirst-fit returns first fitting block? %s\n", (ff == first) ? "YES" : "NO (bug)");

    set_strategy(BEST_FIT);
    block_meta *bf = find_free_block(req);
    int best_ok = (bf != NULL);
    for (block_meta *c = global_head; c && best_ok; c = c->next)
        if (c->free && c->size >= req && c->size < bf->size) best_ok = 0;
    printf("best-fit returns smallest fitting block? %s\n", best_ok ? "YES" : "NO (bug)");

    set_strategy(FIRST_FIT);   // restore default

    return 0;
};