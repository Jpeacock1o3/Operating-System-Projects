/*
 * mm.c
 *
 * Programmer   : Prateek Chandra
 * Last Modified: 2/16/2018
 * Course       : CMPSC 473
 * Description  : In this lab I wrote a dynamic storage allocator for C programs, i.e., my own version of the malloc, free and realloc routines. 
 * Score        : It gives 80.8% optimization when I run ./driver.pl
 *
 */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdbool.h>
#include <stdint.h>

#include "mm.h"
#include "memlib.h"

/*
 * If you want debugging output, uncomment the following. Be sure not
 * to have debugging enabled in your final submission
 */
// #define DEBUG

#ifdef DEBUG
/* When debugging is enabled, the underlying functions get called */
#define dbg_printf(...) printf(__VA_ARGS__)
#define dbg_assert(...) assert(__VA_ARGS__)
#else
/* When debugging is disabled, no code gets generated */
#define dbg_printf(...)
#define dbg_assert(...)
#endif /* DEBUG */

/* do not change the following! */
#ifdef DRIVER
/* create aliases for driver tests */
#define malloc mm_malloc
#define free mm_free
#define realloc mm_realloc
#define calloc mm_calloc
#define memset mem_memset
#define memcpy mem_memcpy
#endif /* DRIVER */

/* What is the correct alignment? */
static uint64_t ALIGNMENT  = 16;    //double word
static uint64_t WSIZE = 8 ;         //word size
static uint64_t CHUNKSIZE = 2048;   //The heap size extension
static char *heap_listp;            //HEAP LIST POINTER
char *segregated_free_list[11];     //segregated list
// total 120 bytes, the limit is 128 bytes of global variables

/* rounds up to the nearest multiple of ALIGNMENT which is a double word*/
static size_t align(size_t x)
{
    return ALIGNMENT * ((x+ALIGNMENT-1)/ALIGNMENT);
}

// *********************************Helper Functions Begining*************************************

/* returns the maximum size from x or y*/
static int MAX (size_t x, size_t y)
{
    return ((x)>(y)?(x):(y));
}
/* returns the minimum size from x and y */
static size_t MIN (size_t x, size_t y)
{
    return ((x)<(y)?(x):(y));
}
/* combine the size and allocated bit to a word for the header/footer*/
static uint64_t PACK (int size, int alloc)
{
    return (uint64_t)(size | alloc);
}
/* read the word at the address p*/
static size_t GET (char *p)
{
    return (*(size_t *)(p));
}
/* read the address of the pointer */
static char *GET_ADDRESS (char *p)
{
    return ((char *)(*(size_t *)(p)));
}
/*write the value in the address */
static void PUT ( char *p, size_t val)
{
    (*(size_t *)(p)) = val;
}
/*write the address of val in the address bp */
static void PUT_ADDRESS (char *bp, char *val)
{
    (*(size_t *)(bp)) = (uint64_t)val;
}
/*read the size of the block from the address */
static uint64_t GET_SIZE (char *p)
{
    return (uint64_t)GET(p) & ~0X7;
}
/*read the allocated bit of the block from the address */
static uint64_t GET_ALLOC (char *p)
{
    return (uint64_t)GET(p) & 0x1;
}
/* Given a block pointer, compute the address of the header */
static char *H_ADDRESS (char *bp)
{
    return ((char *)(bp) - WSIZE);
}
/* Given a block pointer, compute the address of the footer */
static char *F_ADDRESS (char *bp)
{
    return ((char *)(bp) + GET_SIZE(H_ADDRESS(bp)));
}
/* Given a block pointer bp, compute the address of the next block*/
static char *NEXT_BLOCK (char * bp)
{
    return ((char *)(bp) + GET_SIZE(H_ADDRESS(bp)) + ALIGNMENT);
}
/* Given a block pointer bp, compute the address of the previous block*/
static char *PREV_BLOCK (char * bp)
{
    return ((char *)(bp) - (GET_SIZE(((char *)(bp) - ALIGNMENT)) + ALIGNMENT));
}
/* address of the Predecessor */
static char *PRED_ADDR (char *bp)
{
    return ((char *)(bp));
}
/* Address of the Successor*/
static char *SUCC_ADDR (void *bp)
{
    return (char *)((char *)(bp) + WSIZE);
}
/* get index for the segregated list */
static int get_index(size_t size)
{
    if (size <= 32)
    {
        return 0;
    }
    else if (size <= 64)
    {
        return 1;
    }
    else if (size <= 128)
    {
        return 2;
    }
    else if (size <= 256)
    {
        return 3;
    }
    else if (size <= 512)
    {
        return 4;
    }
    else if (size <= 1024)
    {
        return 5;
    }
    else if (size <= 2048)
    {
        return 6;
    }
    else if (size <= 4096)
    {
        return 7;
    }
    else if (size <= 8192)
    {
        return 8;
    }
    else if (size <= 16384)
    {
        return 9;
    }
    else //if (size > 8192)
    {
        return 10;
    }
}

static void add_seg_list (char *bp, size_t size)
{
    //address of the begining of a list
    char *list_begin = mem_heap_lo();
    //address pointing to the address of the begining of the segragated list
    char *seg_start;

    int index = get_index(size);

    seg_start = segregated_free_list[index];
    list_begin = (char *)(seg_start);
    
    //if the begining of the list is empty
    if (list_begin == mem_heap_lo())
    {
        //sets the current block as the first block
        segregated_free_list[index] = (char *) bp;
        //put the current free blocks pointer to the previous block to NULL because it is the first block
        PUT_ADDRESS(PRED_ADDR(bp), mem_heap_lo());
        //put the current free block pointer to the next block to NULL because it is the only block
        PUT_ADDRESS(SUCC_ADDR(bp), mem_heap_lo());
        
    }
    else //if there is already a free block, put the current block as the first block as the first block pointing to the previous first block
    {
        //sets the current block as the first block
        segregated_free_list[index] = (char *) bp;
        //put the current free blocks pointer to the previous block to NULL because it is the first block
        PUT_ADDRESS(PRED_ADDR(bp), mem_heap_lo());
        //put the current free block pointer to the next block to NULL because it is the only block
        PUT_ADDRESS(SUCC_ADDR(bp), list_begin);
        //setting the predecessor of the next block to the current block
        PUT_ADDRESS(PRED_ADDR(list_begin), bp);
        
    }
}

static void remove_seg_list (char *bp, size_t size)
{
    //address of the previous block
    char *previous = GET_ADDRESS(PRED_ADDR(bp));
    //address of the previous block
    char *next = GET_ADDRESS(SUCC_ADDR(bp));
    //the index of the seg list at which the free block will be removed
    int index = get_index(size);
    
    //printf("pointer in remove seg list: %p\n", bp);
    if(previous == mem_heap_lo() && next == mem_heap_lo())        //if the one thats being removed is the only one in the seg list
    {
        segregated_free_list[index] = mem_heap_lo();
    }
    else if (previous == mem_heap_lo() && next != mem_heap_lo())  //if the one thats being removed is the first one in the seg list
    {
        //predecessor of the next block is pointing to NULL
        PUT_ADDRESS(PRED_ADDR(next), mem_heap_lo());
        //update the segregated_free_list
        segregated_free_list[index] = next;
    }
    else if (previous != mem_heap_lo() && next != mem_heap_lo())  //if the one thats being removed is the middle one in the seg list
    {
        //successor of the previous block is pointing to the next block
        PUT_ADDRESS(SUCC_ADDR(previous), next);
        //predecessor of the next block is pointing to the previous block 
        PUT_ADDRESS(PRED_ADDR(next), previous); 
    }
    else if (previous != mem_heap_lo() && next == mem_heap_lo())  //if the one thats being removed is the last one in the seg list
    {
        PUT_ADDRESS(SUCC_ADDR(previous), mem_heap_lo());
    }
}
// *********************************Helper Functions Ending*************************************
/* merging the free blocks */
static void * coalesce (void *bp)
{
    size_t prev_alloc = GET_ALLOC(H_ADDRESS(PREV_BLOCK(bp)));
    size_t next_alloc = GET_ALLOC(H_ADDRESS(NEXT_BLOCK(bp)));
    
    size_t size = GET_SIZE(H_ADDRESS(bp));

    if (prev_alloc && next_alloc) // if the previous block and the next block are allocated
    {
        add_seg_list(bp, size);
        return bp;
    }
    else if (prev_alloc && !next_alloc)// if the previous block is allocated but the next block is free
    {
        
        //getting the size of the previous block and adding it
        size_t next_size = GET_SIZE(H_ADDRESS(NEXT_BLOCK(bp)));
        size = size + next_size + ALIGNMENT;
        
        //remove the next free block
        remove_seg_list(NEXT_BLOCK(bp), next_size);

        //update the header and the footer
        PUT (F_ADDRESS(NEXT_BLOCK(bp)), PACK(size, 0));
        PUT (H_ADDRESS(bp), PACK(size, 0));
        
    }
    else if (!prev_alloc && next_alloc) // if the next block is allocated but the previous block is free
    {
        //getting the size of the previous block and adding it
        size_t previous_size = GET_SIZE((char *)bp-ALIGNMENT);
        
        //remove the previous free block
        remove_seg_list(PREV_BLOCK(bp), previous_size);

        size =  size + previous_size + ALIGNMENT;

        //update the header and the footer
        PUT (H_ADDRESS(PREV_BLOCK(bp)), PACK(size, 0));
        PUT (F_ADDRESS(bp), PACK(size, 0));
        
        //since the block is filled from left to right, now it points to the previous block
        bp = PREV_BLOCK(bp);
    }
    else if (!prev_alloc && !next_alloc) //if the next and the previous blocks are free
    {
        //getting the size of the previous block and the next block and adding it
        size_t previous_size = GET_SIZE((char*) bp-ALIGNMENT);
        size_t next_size = GET_SIZE(H_ADDRESS(NEXT_BLOCK(bp)));
        size = size + previous_size + next_size + ALIGNMENT + ALIGNMENT;

        //remove the previous and the next block from the segregated list
        remove_seg_list(PREV_BLOCK(bp), previous_size);
        remove_seg_list(NEXT_BLOCK(bp), next_size);

        //update the header and the footer
        PUT(H_ADDRESS(PREV_BLOCK(bp)),PACK(size, 0));
        PUT(F_ADDRESS(NEXT_BLOCK(bp)),PACK(size, 0));

        //since the block is filled from left to right, now it points to the previous block
        bp = PREV_BLOCK(bp);
    }

    //add it to the seg list;
    add_seg_list(bp, size);
    return bp;
}
/* Extends the size of the heap and updates the header, footer and the epilogue*/
static void *extend_heap (size_t words)
{
    char *bp;
    size_t size;

    size = (words % 2) ? (words + 1) * WSIZE : words * WSIZE;
    if ((long)(bp = mem_sbrk(size+ALIGNMENT)) == -1)
    {
        return NULL;
    }
    PUT (H_ADDRESS(bp), PACK(size,0)); //Free block header
    PUT (F_ADDRESS(bp), PACK(size,0)); //Free block footer
    PUT (H_ADDRESS(NEXT_BLOCK(bp)), PACK(0,1)); //New Epilogue Header
    return coalesce (bp);
}


/* implicit list allocator */
static void *find_fit(size_t asize)
{
    int index = get_index(asize);
    //address of the begining of a list
    char *seg_start = segregated_free_list[index];
    //address pointing to the address of the begining of the segragated list
    char *list_begin = (char *)(seg_start);

    for (int i = index; i < 11; i++)
    {
        //first case is when the seg list is empty, go to the next

        //loop multiple times in the case that there are multiple empty lists
        while ((char *)segregated_free_list[i]==mem_heap_lo())
        {
            i ++; 
            if(i>10)
            {
                break;
            }  
        }
        if(i>10)
        {
            break;
        }
        
        list_begin = (char *)segregated_free_list[i];
        if(asize <= GET_SIZE(H_ADDRESS(list_begin)))
        {
            return list_begin;
        } 
        //loop through the list to get the last 
        while (GET_ADDRESS(SUCC_ADDR(list_begin)) != mem_heap_lo())
        {
            list_begin = GET_ADDRESS(SUCC_ADDR(list_begin));
            if(asize <= GET_SIZE(H_ADDRESS(list_begin)))
            {
                return list_begin;
            }
        }     
    }
    return NULL;
}
/*places the requested block in the new free block, optionally splitting the block, and then returns a pointer to the newly allocated block.*/
static void place(void *bp, size_t asize)
{
    size_t csize = GET_SIZE(H_ADDRESS(bp));
    //printf("hello123\n");
    if ((int64_t)((csize - asize) - ALIGNMENT) >= (int64_t)ALIGNMENT)
    {
        remove_seg_list(bp, csize);

        PUT(H_ADDRESS(bp), PACK(asize, 1));
        PUT(F_ADDRESS(bp), PACK(asize, 1));

        //assign the remaining blocks and make it free
        PUT(H_ADDRESS(NEXT_BLOCK(bp)), PACK (csize - asize - ALIGNMENT, 0));
        PUT(F_ADDRESS(NEXT_BLOCK(bp)), PACK (csize - asize - ALIGNMENT, 0));

        add_seg_list(NEXT_BLOCK(bp), csize - asize - ALIGNMENT);
    }
    else
    {
        //printf("hello1\n");
        remove_seg_list(bp, csize);
        PUT(H_ADDRESS(bp), PACK(csize, 1));
        PUT(F_ADDRESS(bp), PACK(csize, 1));
    }
}
/*
 * Initialize: return false on error, true on success.
 */
bool mm_init(void)
{
    //creating the initial seg_list
    for (int i = 0; i < 11; i++)
    {
        segregated_free_list[i] = mem_heap_lo();
    }

    //void * heap_listp;
    heap_listp = mem_sbrk(4*WSIZE);
    if ( heap_listp == (void *)-1)
    {
        return false;
    }

    PUT(heap_listp, 0);                         //allignment padding
    PUT(heap_listp + (1*WSIZE), PACK(0, 1));    //Prologue header
    PUT(heap_listp + (2*WSIZE), PACK(0, 1));    //Prologue footer
    PUT(heap_listp + (3*WSIZE), PACK(0, 1));    //Epilogue header
    heap_listp += (2*WSIZE);

    //extenf the heap with a free block of the size allocated
    if (extend_heap(CHUNKSIZE/WSIZE)==NULL)
    {
        return false;
    }
    return true;
}
/*
 * malloc
 */
void* malloc(size_t size)
{
    size_t asize; // new size adjusted by the allignment requirement
    size_t extendsize;
    char *bp;

    if (size==0)
    {
        return NULL;
    }

    //adjust the block size according to the allignment requirements
    if (size > ALIGNMENT)
    {
        asize = align(size);
    }
    else
    {
        asize = ALIGNMENT;
    }
    //search the free list for a fit
    if ((bp = find_fit(asize))!=NULL)
    {
        place(bp, asize);
        return bp;
    }
    //if there is no free, increase the size
    extendsize = MAX (asize, CHUNKSIZE);
    if((bp = extend_heap(extendsize/WSIZE))==NULL)
    {
        return NULL;
    }
    else
    {
        bp = find_fit(asize);
        place(bp, asize);
    }
    return bp;
}

/*
 * free
 */
void free(void* bp)
{
    if (bp != NULL)
    {
        size_t size = GET_SIZE(H_ADDRESS(bp));
        PUT(H_ADDRESS(bp), PACK (size, 0));
        PUT(F_ADDRESS(bp), PACK (size, 0));
        //printf("")
        coalesce(bp);
    }
}

/*
 * realloc
 */
void* realloc(void* oldptr, size_t size)
{
    //All possible cases that are possible when reallocing: 
    //size = 0
    //oldptr = NULL
    //new size = current size => return the old pointer
    //new size < current size => keep the same pointer where it is, adjust the headers and footers and then split it
    //new size > current size => malloc the new size anywhere then copy it, then free it

    if (size == 0)
    {
        free (oldptr);
        //return NULL;
    }
    else if (oldptr==NULL)
    {
        return malloc (size);
    }
    else
    {
        //getting the size of the old pointer
        size_t old_size = GET_SIZE(H_ADDRESS(oldptr));
        size = align(size);
        if (old_size == size)//new size = current size => return the old pointer
        {
            return oldptr;
        }
        else if (old_size > size)//new size < current size => keep the same pointer where it is, adjust the headers and footers and then split it
        {
            //first allign the size
            
            //size_t csize = GET_SIZE(H_ADDRESS(bp));
            //printf("hello123\n");
            if ((int64_t)(old_size - size - ALIGNMENT) >= (int64_t)ALIGNMENT)
            {
                //remove_seg_list(oldptr, size);
        
                PUT(H_ADDRESS(oldptr), PACK(size, 1));
                PUT(F_ADDRESS(oldptr), PACK(size, 1));
                
                PUT(H_ADDRESS(NEXT_BLOCK(oldptr)), PACK((old_size - size - ALIGNMENT), 0));
                PUT(F_ADDRESS(NEXT_BLOCK(oldptr)), PACK((old_size - size - ALIGNMENT), 0));
        
                //add_seg_list(oldptr, align(old_size - size - ALIGNMENT));

                coalesce(NEXT_BLOCK(oldptr));
                
            }
            else
            {
                //printf("hello1\n");
                //remove_seg_list(oldptr, size);
                PUT(H_ADDRESS(oldptr), PACK(old_size, 1));
                PUT(F_ADDRESS(oldptr), PACK(old_size, 1));
                
                //coalesce(NEXT_BLOCK(oldptr));
                //add_seg_list(oldptr, old_size - size - ALIGNMENT);
            }
            return oldptr;
        }
        else if (old_size < size)//new size > current size => malloc the new size anywhere then copy it, then free the OLD ptr
        {
            
            char *new_pointer = malloc(size);

            if(new_pointer==NULL)
            {
                return NULL;
            }

            memcpy(new_pointer, oldptr, old_size);
            free((oldptr));

            return new_pointer;
        }
    }
    return NULL;
}

/*
 * calloc
 * This function is not tested by mdriver, and has been implemented for you.
 */
void* calloc(size_t nmemb, size_t size)
{
    void* ret;
    size *= nmemb;
    ret = malloc(size);
    if (ret) {
        memset(ret, 0, size);
    }
    return ret;
}

/*
 * Return whether the pointer is in the heap.
 * May be useful for debugging.
 */
static bool in_heap(const void* p)
{
    return p <= mem_heap_hi() && p >= mem_heap_lo();
}

/*
 * Return whether the pointer is aligned.
 * May be useful for debugging.
 */
static bool aligned(const void* p)
{
    size_t ip = (size_t) p;
    return align(ip) == ip;
}

/*
 * mm_checkheap
 */
bool mm_checkheap(int lineno)
{
#ifdef DEBUG
    /* Write code to check heap invariants here */
    /* IMPLEMENT THIS */
#endif /* DEBUG */
    return true;
}
