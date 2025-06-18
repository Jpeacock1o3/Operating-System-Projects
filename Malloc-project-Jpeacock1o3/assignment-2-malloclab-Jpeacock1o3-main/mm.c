/*
 * mm.c
 *
 * Name: Jaden Peacock
 *
 * High-Level Description:
 * This assignment creates a dynamic memory allocator that provides
 * malloc, free, realloc, and calloc functions. It uses segregated free
 * lists and boundary tag coalescing to manage free memory blocks efficiently
 */

 #include <assert.h>
 #include <stdlib.h>
 #include <stdio.h>
 #include <string.h>
 #include <unistd.h>
 #include <stdint.h>
 #include <stdbool.h>
 
 #include "mm.h"
 #include "memlib.h"
 
 /*
 * If you want to enable your debugging output and heap checker code,
 * uncomment the following line. Be sure not to have debugging enabled
 * in your final submission.
 */
 // #define DEBUG
 
 #ifdef DEBUG
 // When debugging is enabled, the underlying functions get called
 #define dbg_printf(...) printf(__VA_ARGS__)
 #define dbg_assert(...) assert(__VA_ARGS__)
 #else
 // When debugging is disabled, no code gets generated
 #define dbg_printf(...)
 #define dbg_assert(...)
 #endif // DEBUG
 

 // do not change the following!
 #ifdef DRIVER
 // create aliases for driver tests
 #define malloc mm_malloc
 #define free mm_free
 #define realloc mm_realloc
 #define calloc mm_calloc
 #define memset mm_memset
 #define memcpy mm_memcpy
 #endif // DRIVER
 
 #define ALIGNMENT 16
 #define WSIZE 8         // Word size (bytes)
 #define CHUNKSIZE 2048  // Initial heap size (bytes)
 
 // Helper functions for manipulating headers, footers, and block pointers
 
 /* Pack a size and allocated bit into a word. */
 static inline size_t pack(size_t size, int alloc) {
     return size | alloc;
 }
 
 /* Read a word at address p. */
 static inline size_t get(void *p) {
     return *(size_t *)p;
 }
 
 /* Write a word at address p. */
 static inline void put(void *p, size_t val) {
     *(size_t *)p = val;
 }
 
 /* Extract the block size from header/footer word. */
 static inline size_t get_size(void *p) {
     return get(p) & ~0x7;
 }
 
 /* Extract the allocated bit from header/footer word. */
 static inline int get_alloc(void *p) {
     return get(p) & 0x1;
 }
 
 /* Given a pointer to payload, return pointer to its header. */
 static inline void *hdrp(void *ptr) {
     return (char *)ptr - WSIZE;
 }
 
 /* Given a pointer to payload, return pointer to its footer.
    (Subtracting 2*WSIZE because header and footer each take one word.) */
 static inline void *ftrp(void *ptr) {
     return (char *)ptr + get_size(hdrp(ptr)) - 2*WSIZE;
 }
 
 /* Return pointer to next block's payload in the heap. */
 static inline void *next_blkp(void *ptr) {
     return (char *)ptr + get_size(hdrp(ptr));
 }
 
 /* Return pointer to previous block's payload in the heap. */
 static inline void *prev_blkp(void *ptr) {
     return (char *)ptr - get_size((char *)ptr - 2*WSIZE);
 }
 
 /* 
  * Free block pointer manipulation:
  * The free block stores pointers to its predecessor and successor in its payload.
  */
 static inline char *pred_ptr(void *ptr) {
     return (char *)ptr;
 }
 
 static inline char *succ_ptr(void *ptr) {
     return (char *)ptr + WSIZE;
 }
 
 static inline void *get_pred(void *ptr) {
     return *(char **)pred_ptr(ptr);
 }
 
 static inline void *get_succ(void *ptr) {
     return *(char **)succ_ptr(ptr);
 }
 
 static inline void set_pred(void *ptr, void *val) {
     *(char **)pred_ptr(ptr) = val;
 }
 
 static inline void set_succ(void *ptr, void *val) {
     *(char **)succ_ptr(ptr) = val;
 }
 
 // Function prototypes for longer functions
 static void *extend_heap(size_t words);
 static void *coalesce(void *ptr);
 static void *find_fit(size_t asize);
 static void place(void *ptr, size_t asize);
 static void add_to_free_list(void *ptr);
 static void remove_from_free_list(void *ptr);
 static int get_list_index(size_t size);
 
 // Global variables
 static char *heap_listp = 0;  // Pointer to first block in heap
 static char *free_lists[11];  // Array of segregated free lists
 
 // rounds up to the nearest multiple of ALIGNMENT
 static size_t align(size_t x)
 {
     return ALIGNMENT * ((x+ALIGNMENT-1)/ALIGNMENT);
 }
 
 // mm_init: Initialize the memory manager.
 // Initializes free lists.
 // Creates an initial empty heap with prologue and epilogue.
 // Extends the heap with an initial free block.
 bool mm_init(void)
 {
     for (int i = 0; i < 11; i++) {
         free_lists[i] = NULL;
     }
     
     if ((heap_listp = mm_sbrk(4*WSIZE)) == (void *)-1) {
         return false;
     }
     
     put(heap_listp, 0);                              // Alignment padding
     put(heap_listp + (1*WSIZE), pack(WSIZE*2, 1));     // Prologue header
     put(heap_listp + (2*WSIZE), pack(WSIZE*2, 1));     // Prologue footer
     put(heap_listp + (3*WSIZE), pack(0, 1));           // Epilogue header
     heap_listp += (2*WSIZE);
     
     if (extend_heap(CHUNKSIZE/WSIZE) == NULL) {
         return false;
     }
     
     return true;
 }
 
 // malloc: Allocate a block with at least 'size' bytes of payload.
 // Adjusts size to include header/footer overhead and align the block.
 // Searches the free list for a fit; if none, extends the heap.
 void* malloc(size_t size)
 {
     size_t asize;      // Adjusted block size
     size_t extendsize; // Amount to extend heap if no fit found
     char *ptr;          
     
     if (size == 0) {
         return NULL;
     }
     
     if (size <= WSIZE*2) {
         asize = WSIZE*4;  // Minimum block size (16 bytes payload + 16 bytes overhead)
     } else {
         asize = align(size + WSIZE*2);  // Include overhead for header and footer
     }
     
     if ((ptr = find_fit(asize)) != NULL) {
         place(ptr, asize);
         return ptr;
     }
     
     extendsize = (asize > CHUNKSIZE) ? asize : CHUNKSIZE;
     if ((ptr = extend_heap(extendsize/WSIZE)) == NULL) {
         return NULL;
     }
     place(ptr, asize);
     return ptr;
 }
 
 // free: Free an allocated block.
 // Marks the block as free and updates its header/footer.
 // Attempts to coalesce with adjacent free blocks.
 void free(void* ptr)
 {
     if (ptr == NULL) {
         return;
     }
     
     size_t size = get_size(hdrp(ptr));
     
     put(hdrp(ptr), pack(size, 0));
     put(ftrp(ptr), pack(size, 0));
     
     coalesce(ptr);
 }
 
 
 // realloc: Reallocate a block to a new size.
 // If the new size is smaller, shrink the block (split if possible).
 // If larger, attempt in-place extension; otherwise, allocate a new block.
 void* realloc(void* oldptr, size_t size)
 {
     if (size == 0) {
         free(oldptr);
         return NULL;
     }
     if (oldptr == NULL)
         return malloc(size);
     
     size_t oldsize = get_size(hdrp(oldptr));
     size_t newsize = align(size + WSIZE*2);  // New block size including overhead
     
     if (newsize <= oldsize) {
         if (oldsize - newsize >= (4*WSIZE)) {
             put(hdrp(oldptr), pack(newsize, 1));
             put(ftrp(oldptr), pack(newsize, 1));
             
             void *next_ptr = next_blkp(oldptr);
             put(hdrp(next_ptr), pack(oldsize - newsize, 0));
             put(ftrp(next_ptr), pack(oldsize - newsize, 0));
             add_to_free_list(next_ptr);
         }
         return oldptr;
     }
     
     // Attempt to extend block in place if the next block is free.
     void *next_ptr = next_blkp(oldptr);
     if (!get_alloc(hdrp(next_ptr))) {
         size_t combined_size = oldsize + get_size(hdrp(next_ptr));
         if (combined_size >= newsize) {
             remove_from_free_list(next_ptr);
             put(hdrp(oldptr), pack(combined_size, 1));
             put(ftrp(oldptr), pack(combined_size, 1));
             // Split if the excess space is sufficient.
             if (combined_size - newsize >= (4*WSIZE)) {
                 put(hdrp(oldptr), pack(newsize, 1));
                 put(ftrp(oldptr), pack(newsize, 1));
                 
                 void *remainder = next_blkp(oldptr);
                 put(hdrp(remainder), pack(combined_size - newsize, 0));
                 put(ftrp(remainder), pack(combined_size - newsize, 0));
                 add_to_free_list(remainder);
             }
             return oldptr;
         }
     }
     
     // Otherwise, allocate a new block, copy the data, and free the old block.
     void *newptr = malloc(size);
     if (newptr == NULL)
         return NULL;
     
     size_t copySize = oldsize - (WSIZE*2);
     if (size < copySize)
         copySize = size;
     memcpy(newptr, oldptr, copySize);
     free(oldptr);
     return newptr;
 }
 
 /*
 * calloc
 * This function is not tested by mdriver, and has been implemented for you.
 */
 void* calloc(size_t nmemb, size_t size)
 {
     void* ptr;
     size *= nmemb;
     ptr = malloc(size);
     if (ptr) {
         memset(ptr, 0, size);
     }
     return ptr;
 }
 
 /*
 * Returns whether the pointer is in the heap.
 * May be useful for debugging.
 */
 static bool in_heap(const void* p)
 {
     return p <= mm_heap_hi() && p >= mm_heap_lo();
 }
 
 /*
 * Returns whether the pointer is aligned.
 * May be useful for debugging.
 */
 static bool aligned(const void* p)
 {
     size_t ip = (size_t) p;
     return align(ip) == ip;
 }
 
 /*
 * mm_checkheap
 * You call the function via mm_checkheap(__LINE__)
 * The line number can be used to print the line number of the calling
 * function where there was an invalid heap.
 */
 bool mm_checkheap(int line_number)
 {
 #ifdef DEBUG
     if (get_size(hdrp(heap_listp)) != WSIZE*2 || !get_alloc(hdrp(heap_listp))) {
         dbg_printf("Line %d: Prologue header invalid\n", line_number);
         return false;
     }
     
     char *ptr = heap_listp;
     while (get_size(hdrp(ptr)) > 0) {
         if (!aligned(ptr)) {
             dbg_printf("Line %d: Block at %p not aligned\n", line_number, ptr);
             return false;
         }
         
         if (get(hdrp(ptr)) != get(ftrp(ptr))) {
             dbg_printf("Line %d: Header/footer mismatch for block at %p\n", line_number, ptr);
             return false;
         }
         
         ptr = next_blkp(ptr);
     }
     
     if (!get_alloc(hdrp(ptr)) || get_size(hdrp(ptr)) != 0) {
         dbg_printf("Line %d: Epilogue header invalid\n", line_number);
         return false;
     }
 #endif // DEBUG
     return true;
 }
 

 // extend_heap: Extend the heap by allocating new memory.
 // Allocates an even number of words to maintain alignment.
 // Initializes a new free block and the new epilogue.
 // Coalesces with the previous block if possible.

 static void *extend_heap(size_t words)
 {
     char *ptr;
     size_t size;
     
     size = (words % 2) ? (words+1) * WSIZE : words * WSIZE;
     if ((long)(ptr = mm_sbrk(size)) == -1) {
         return NULL;
     }
     
     put(hdrp(ptr), pack(size, 0));         // Free block header
     put(ftrp(ptr), pack(size, 0));           // Free block footer
     put(hdrp(next_blkp(ptr)), pack(0, 1));   // New epilogue header
     
     return coalesce(ptr);
 }
 

 // coalesce: Merge adjacent free blocks.
 //  Checks if the previous and/or next blocks are free and merges accordingly.
 //  Updates block headers, footers, and free lists.

 static void *coalesce(void *ptr)
 {
     size_t prev_alloc = get_alloc(ftrp(prev_blkp(ptr))) || prev_blkp(ptr) == ptr;
     size_t next_alloc = get_alloc(hdrp(next_blkp(ptr)));
     size_t size_val = get_size(hdrp(ptr));
     
     if (prev_alloc && next_alloc) {      // Case 1: Both neighbors allocated
         add_to_free_list(ptr);
         return ptr;
     }
     else if (prev_alloc && !next_alloc) { // Case 2: Next block free
         void *next_ptr = next_blkp(ptr);
         remove_from_free_list(next_ptr);
         
         size_val += get_size(hdrp(next_ptr));
         put(hdrp(ptr), pack(size_val, 0));
         put(ftrp(ptr), pack(size_val, 0));
     }
     else if (!prev_alloc && next_alloc) { // Case 3: Previous block free
         void *prev_ptr = prev_blkp(ptr);
         remove_from_free_list(prev_ptr);
         
         size_val += get_size(hdrp(prev_ptr));
         put(ftrp(ptr), pack(size_val, 0));
         put(hdrp(prev_ptr), pack(size_val, 0));
         ptr = prev_ptr;
     }
     else {                               // Case 4: Both neighbors free
         void *prev_ptr = prev_blkp(ptr);
         void *next_ptr = next_blkp(ptr);
         remove_from_free_list(prev_ptr);
         remove_from_free_list(next_ptr);
         
         size_val += get_size(hdrp(prev_ptr)) + get_size(hdrp(next_ptr));
         put(hdrp(prev_ptr), pack(size_val, 0));
         put(ftrp(next_ptr), pack(size_val, 0));
         ptr = prev_ptr;
     }
     
     add_to_free_list(ptr);
     return ptr;
 }
 
 
 // find_fit: Find a free block that fits at least asize bytes.
 //  Uses a best-fit approach over segregated free lists to reduce waste.
 
 static void *find_fit(size_t asize)
 {
     int index = get_list_index(asize);
     void *best_ptr = NULL;
     size_t best_size = (size_t)-1;
     
     // Search free lists starting at the bin corresponding to asize.
     for (int i = index; i < 11; i++) {
         for (void *ptr = free_lists[i]; ptr != NULL; ptr = get_succ(ptr)) {
             size_t block_size = get_size(hdrp(ptr));
             if (block_size >= asize && block_size < best_size) {
                 best_ptr = ptr;
                 best_size = block_size;
             }
         }
         // Return immediately if an exact fit is found.
         if (best_ptr != NULL && best_size == asize)
             return best_ptr;
     }
     return best_ptr;
 }
 
 
 // place: Place a block of asize bytes into the free block pointed to by ptr.
 //  Splits the block if the remainder is large enough for a new free block.
 
 static void place(void *ptr, size_t asize)
 {
     size_t csize = get_size(hdrp(ptr));
     
     remove_from_free_list(ptr);
     
     if ((csize - asize) >= (4*WSIZE)) {
         put(hdrp(ptr), pack(asize, 1));
         put(ftrp(ptr), pack(asize, 1));
         
         void *next_ptr = next_blkp(ptr);
         put(hdrp(next_ptr), pack(csize - asize, 0));
         put(ftrp(next_ptr), pack(csize - asize, 0));
         add_to_free_list(next_ptr);
     }
     else {
         put(hdrp(ptr), pack(csize, 1));
         put(ftrp(ptr), pack(csize, 1));
     }
 }
 
 
 // add_to_free_list: Insert a free block into the appropriate segregated list.
 //  Uses LIFO insertion (inserting at the head).
 
 static void add_to_free_list(void *ptr)
 {
     size_t size_val = get_size(hdrp(ptr));
     int index = get_list_index(size_val);
     
     set_pred(ptr, NULL);
     set_succ(ptr, free_lists[index]);
     
     if (free_lists[index] != NULL) {
         set_pred(free_lists[index], ptr);
     }
     
     free_lists[index] = ptr;
 }
 
 
 // remove_from_free_list: Remove a block from its segregated free list.
 //  Performs a defensive check to ensure the block is in the list.
 
 static void remove_from_free_list(void *ptr)
 {
     size_t size_val = get_size(hdrp(ptr));
     int index = get_list_index(size_val);
     
     void *current = free_lists[index];
     bool found = false;
     while (current != NULL) {
         if (current == ptr) {
             found = true;
             break;
         }
         current = get_succ(current);
     }
     if (!found) {
         return;
     }
     
     if (get_pred(ptr) != NULL) {
         set_succ(get_pred(ptr), get_succ(ptr));
     } else {
         free_lists[index] = get_succ(ptr);
     }
     
     if (get_succ(ptr) != NULL) {
         set_pred(get_succ(ptr), get_pred(ptr));
     }
 }
 
 
 // get_list_index: Determine the appropriate free list bin based on block size.
 
 static int get_list_index(size_t size)
 {
     if (size <= 32) return 0;
     else if (size <= 64) return 1;
     else if (size <= 128) return 2;
     else if (size <= 256) return 3;
     else if (size <= 512) return 4;
     else if (size <= 1024) return 5;
     else if (size <= 2048) return 6;
     else if (size <= 4096) return 7;
     else if (size <= 8192) return 8;
     else if (size <= 16384) return 9;
     else return 10;
 }
 