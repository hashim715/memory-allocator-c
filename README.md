# memory_allocator

A custom memory allocator written in C — building `malloc`/`free` from scratch using `mmap`.

This is a learning project exploring how low-level memory allocators work under the hood, step by step.

## Progress

- **step1_bump.c** — Reads the current program break with `sbrk(0)` to inspect the process's initial memory layout.
- **main.c** — Requests memory directly from the OS using `mmap`, instead of relying on the deprecated `sbrk`. Includes a `block_meta` header (size/free/next) placed at the start of each `mmap`'d chunk, with `block_to_ptr`/`ptr_to_block` helpers to convert between the header and the user-facing pointer, and a `make_block()` helper that mmaps a chunk and initializes its header.

  The allocator now tracks all blocks in a singly linked list (`global_head`/`global_tail`) and exposes a real `my_malloc`/`my_free` API:
  - `my_malloc(size)` first calls `find_free_block()` to scan the list for a previously freed block big enough to reuse; only if none is found does it `mmap` a new block and append it to the tail of the list.
  - `my_free(ptr)` recovers the block header from the user pointer and marks it free, making it available for reuse by a later `my_malloc` call.
  - `print_list()` walks the list and prints each block's address, size, free status, and `next` pointer, for debugging.

  `main()` exercises this: it allocates a few blocks, frees one and confirms a smaller subsequent allocation reuses that same freed block (including a block in the *middle* of the list, not just the most recently freed one).

More steps (splitting/coalescing freed blocks, handling arbitrary allocation order, a real `realloc`) will be added in upcoming sessions.

## Why `mmap` instead of `sbrk`?

`sbrk` is deprecated on macOS and considered legacy even on Linux. `mmap` is the portable, modern way to request memory from the OS and is what production allocators fall back on for larger allocations.

## Building

Each step is a standalone C file for now:

```sh
clang -o main main.c
./main
```

## Requirements

- A C compiler (`clang` or `gcc`)
- POSIX-compliant OS (macOS/Linux)
