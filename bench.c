#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "allocator.h"
#include <time.h>   // Required for time()

#define NUM_SLOTS 1000
#define NUM_OPERATIONS 100000
#define SEED 42

typedef struct {
    void* ptr;
    size_t requested_size;
} slot;

int random_size() {
    int probability_roll = (rand() % 100) + 1;

    if (probability_roll <= 90) {
        return (rand() % (512 - 16 + 1)) + 16;
    } else {
        return (rand() % (8000 - 512 + 1)) + 512;
    };
};

double now_seconds() {
    struct timespec t;

    clock_gettime(CLOCK_MONOTONIC, &t);

    double total_seconds = t.tv_sec + t.tv_nsec * 1e-9;

    return total_seconds;
};

int main(int argc, char** argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <first|best>\n", argv[0]);
        exit(1);
    };

    if (strcmp(argv[1], "first") == 0) {
        set_strategy(FIRST_FIT);
    } else if (strcmp(argv[1], "best") == 0) {
        set_strategy(BEST_FIT);
    } else {
        fprintf(stderr, "Usage: %s <first|best>\n", argv[0]);
        exit(1);
    };

    srand(SEED);

    slot slots[NUM_SLOTS] = {0};

    heap_stats stats = {0};

    total_bytes_allocated = 0;
    live_bytes_allocated = 0;

    double start = now_seconds();

    for (int i = 0; i < NUM_OPERATIONS; i++) {
        int slot_num = rand() % NUM_SLOTS;
        if(slots[slot_num].ptr == NULL) {
            int requested_size = random_size();

            void* ptr = my_malloc(requested_size);

            if (ptr == NULL) {
                fprintf(stderr, "Error: my_malloc(%d) returned NULL at operation %d (slot %d)\n",
                        requested_size, i, slot_num);
                exit(1);
            };

            memset(ptr,(unsigned char)slot_num,requested_size);

            slots[slot_num].ptr = ptr;
            slots[slot_num].requested_size = requested_size;
            live_bytes_allocated += requested_size;
        } else {
            void* ptr = slots[slot_num].ptr;

            unsigned char* first_byte = ((unsigned char*)ptr) + 0;

            unsigned char* last_byte = ((unsigned char*)ptr) + (slots[slot_num].requested_size - 1);

            unsigned char expected = (unsigned char)slot_num;

            if (*first_byte != expected|| *last_byte != expected) {
                fprintf(stderr, "Error: corruption detected in slot %d at operation %d\n", slot_num, i);
                exit(1);
            };

            my_free(slots[slot_num].ptr);
            live_bytes_allocated -= slots[slot_num].requested_size;
            slots[slot_num].ptr = NULL;
            slots[slot_num].requested_size = 0;
        };
    };

    double end = now_seconds();
    double elapsed = end - start;

    get_heap_stats(&stats);

    double ops_per_sec = NUM_OPERATIONS / elapsed;
    double utilization = stats.heap_bytes
        ? (double)stats.live_bytes / (double)stats.heap_bytes * 100.0 : 0.0;
    double ext_frag = stats.free_bytes
        ? (1.0 - (double)stats.largest_free / (double)stats.free_bytes) * 100.0 : 0.0;

    printf("\n=========== Benchmark: %s ===========\n", current_strategy == FIRST_FIT ? "first fit" : "best fit");
    printf("Operations        : %d  (%d slots, seed %d)\n", NUM_OPERATIONS, NUM_SLOTS, SEED);
    printf("Elapsed time      : %.4f s\n", elapsed);
    printf("Throughput        : %.0f ops/sec\n", ops_per_sec);
    printf("------------------------------------------\n");
    printf("Heap size (OS)    : %zu bytes\n", stats.heap_bytes);
    printf("Live bytes        : %zu bytes\n", stats.live_bytes);
    printf("Utilization       : %.2f%%\n", utilization);
    printf("Fragmentation     : %.2f%%\n", 100.0 - utilization);
    printf("------------------------------------------\n");
    printf("Blocks in list    : %zu\n", stats.block_count);
    printf("Free bytes        : %zu bytes\n", stats.free_bytes);
    printf("Largest free block: %zu bytes\n", stats.largest_free);
    printf("External frag.    : %.2f%%\n", ext_frag);
    printf("==========================================\n");

    return 0;
};