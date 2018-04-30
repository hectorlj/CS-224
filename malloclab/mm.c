/**
 *      In mm_malloc, the block is allocated by traversing the free list and finding the
 *      first free block that has size greater than or equal to the requested size. If the free
 *      block is too big then the free block is split and the first part is allocated
 *      and the rest of the part is kept free. The headers and footers of the new blocks
 *      are changed accordingly.
 *
 *      In mm_free, the block to be freed is found and its header and footer are changed.
 *      The allocated bit is set to 0. The freed block is placed at the beginning of
 *      the free list. If the adjacent blocks are also free, then the freed block
 *      is coalesced with them.
 *
 *      In mm-realloc, if the new size is smaller than the old size, then the block 
 *      is shrunk updating the header/footer. If the new size is greater than old size,
 *      then we allocate a new block with malloc, copy he old data into the new block and
 *      free the old block. If the new size is same as the old size, then the same block is returned.
 * 
 *      In extend_heap, the heap is extended by the even word size asked for. If the word size is
 *      smaller than the overhead then it is set to the overhead. It then places the header
 *      and footer, along with the epilogue header. It then coaleces the previous free block 
 *      which is then added to the list
 *      
 *      In coalesce the new block has its header and footer updated based on which blocks
 *      adjacent to it are free, with the three cases being, free next block, free previous
 *      block, and both next and previous blocks free
 *      
 *      push_block pushes a block to the front of the list
 *
 *      pop_block removes a block from the list
 */
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <string.h>

#include "mm.h"
#include "memlib.h"

/*********************************************************
 * NOTE TO STUDENTS: Before you do anything else, please
 * provide your team information in the following struct.
 ********************************************************/
team_t team = {
    /* Team name */
    "The A Team",
    /* First member's full name */
    "Hector Lopez",
    /* First member's email address */
    "hlj239@byu.edu",
    "",
    ""
};

/* single word (4) or double word (8) alignment */
#define ALIGNMENT 8

/* rounds up to the nearest multiple of ALIGNMENT */
#define ALIGN(size) (((size) + (ALIGNMENT-1)) & ~0x7)


#define SIZE_T_SIZE (ALIGN(sizeof(size_t)))

/*Additional Macros defined*/
#define WSIZE 4 // Size of a word
#define DSIZE 8 // Size of a double word
#define CHUNKSIZE 16    // Initial heap size
#define OVRHD 24    // The minimum block size
#define MAX(x ,y)  ((x) > (y) ? (x) : (y))  // Finds the maximum of two numbers
#define PACK(size, alloc)  ((size) | (alloc))   // Put the size and allocated byte into one word
#define GET(p)  (*(size_t *)(p))    // Read the word at address p
#define PUT(p, value)  (*(size_t *)(p) = (value))   // Write the word at address p
#define GET_SIZE(p)  (GET(p) & ~0x7)    // Get the size from header/footer
#define GET_ALLOC(p)  (GET(p) & 0x1)    // Get the allocated bit from header/footer
#define HDRP(bp)  ((void *)(bp) - WSIZE)    // Get the address of the header of a block
#define FTRP(bp)  ((void *)(bp) + GET_SIZE(HDRP(bp)) - DSIZE)   // Get the address of the footer of a block
#define NEXT_BLKP(bp)  ((void *)(bp) + GET_SIZE(HDRP(bp)))  // Get the address of the next block
#define PREV_BLKP(bp)  ((void *)(bp) - GET_SIZE(HDRP(bp) - WSIZE))  // Get the address of the previous block
#define NEXT_FREE_PTR(bp)  (*(void **)(bp + DSIZE)) // Get the address of the next free block
#define PREV_FREE_PTR(bp)  (*(void **)(bp))   // Get the address of the previous free block

static char *heap_list_ptr = 0;    // Pointer to the first block
static char *free_list_ptr = 0;    // Pointer to the first free block

    // Function prototypes for helper routines
static void *extend_heap(size_t words);
static void place(void *bp, size_t size);
static void *find_fit(size_t size);
static void *coalesce(void *bp);
static void push_block(void *bp);
static void pop_block(void *bp);
// static int check_block(void *bp);

/**
 * mm_init - Initializes the malloc
 *  Returns 0 if successful -1 if unsucessful
 */
int mm_init(void)
{
    if((heap_list_ptr = mem_sbrk(2 * OVRHD)) == NULL){ // Return error if unable to get heap space
        return -1;
    }

    PUT(heap_list_ptr, 0); // Put the Padding at the start of heap
    PUT(heap_list_ptr + WSIZE, PACK(OVRHD, 1));    // Put the header block of the prologue
    PUT(heap_list_ptr + DSIZE, 0); // Put the previous pointer
    PUT(heap_list_ptr + DSIZE + WSIZE, 0); // Put the next pointer
    PUT(heap_list_ptr + OVRHD, PACK(OVRHD, 1));    // Put the footer block of the prologue
    PUT(heap_list_ptr + WSIZE + OVRHD, PACK(0, 1));    // Put the header block of the epilogue
    free_list_ptr = heap_list_ptr + DSIZE;    // Initialize the free list pointer

    if(extend_heap(CHUNKSIZE / WSIZE) == NULL){ // Return error if unable to extend heap space
        return -1;
    }

    return 0;
}

/**
 * mm_malloc - Allocates a block with atleast the specified size of payload
 * Returns the pointer to the start of the allocated block
 */
void *mm_malloc(size_t size)
{
    size_t asize;   // The size of the adjusted block
    size_t extsize; // The amount by which heap is extended if no fit is found
    char *bp;   // Stores the block pointer

    if(size <= 0){  // If requested size is less than 0 then ignore
        return NULL;
    }

    asize = MAX(ALIGN(size) + DSIZE, OVRHD);    // Adjust block size to include OVRHD and alignment requirements

    if((bp = find_fit(asize))){ // Traverse the free list for the first fit
        place(bp, asize);   // Place the block in the free list
        return bp;
    }

    extsize = MAX(asize, CHUNKSIZE);    // If no fit is found get more memory to extend the heap

    if((bp = extend_heap(extsize / WSIZE)) == NULL){    // If unable to extend heap space
        return NULL;    // return null
    }

    place(bp, asize);   // Place the block in the newly extended space
    return bp;
}

/**
 * mm_free - Frees a block
 * bp =The block to be freed
 */
void mm_free(void *bp)
{
    if(!bp){    // If block pointer is null
        return; // return
    }

    size_t size = GET_SIZE(HDRP(bp));   // Get the total block size

    PUT(HDRP(bp), PACK(size, 0));   // Set the header as unallocated
    PUT(FTRP(bp), PACK(size, 0));   // Set the footer as unallocated
    coalesce(bp);   // Coalesce and add the block to the free list
}

/**
 * mm_realloc - Reallocates a block of memory
 * bp =The block pointer of the block to be reallocated
 * size = The size of the block to be reallocated
 * Returns the block pointer to the reallocated block
 */
void *mm_realloc(void *bp, size_t size)
{
    size_t oldsize; // Holds the old size of the block
    void *newbp;    // Holds the new block pointer
    size_t asize = MAX(ALIGN(size) + DSIZE, OVRHD); // Calciulate the adjusted size

    if(size <= 0){  // If size is less than 0
        mm_free(bp);    // Free the block
        return 0;
    }

    if(bp == NULL){ // If old block pointer is null, then it is malloc
        return mm_malloc(size);
    }

    oldsize = GET_SIZE(HDRP(bp));   // Get the size of the old block

    if(oldsize == asize){   // If the size of the old block and requested size are same then return the old block pointer
        return bp;
    }

    if(asize <= oldsize){   // If the size needs to be decreased
        size = asize;   // Shrink the block

        if(oldsize - size <= OVRHD){    // If the new block cannot be formed
            return bp;  // Return the old block pointer
        }
    // If a new block can be formed
        PUT(HDRP(bp), PACK(size, 1));   // Update the size in the header of the reallocated block
        PUT(FTRP(bp), PACK(size, 1));   // Update the size in the header of the reallocated block
        PUT(HDRP(NEXT_BLKP(bp)), PACK(oldsize - size, 1));  // Update the size in the header of the next block
        mm_free(NEXT_BLKP(bp)); // Free the next block
        return bp;
    }
    // If the block has to be expanded during reallocation
    newbp = mm_malloc(size);    // Allocate a new block

    if(!newbp){ // If realloc fails the original block is left as it is
        return 0;
    }

    if(size < oldsize){
        oldsize = size;
    }

    memcpy(newbp, bp, oldsize); // Copy the old data to the new block
    mm_free(bp);    // Free the old block
    return newbp;
}

/**
 * extend_heap Extends the heap with free block
 * words = The size to extend the heap by
 * returns the block pointer to the frst block in the newly acquired heap space
 */
static void* extend_heap(size_t words){
    char *bp;
    size_t size;

    size = (words % 2) ? (words + 1) * WSIZE : words * WSIZE;   // Allocate even number of  words to maintain alignment

    if(size < OVRHD){
        size = OVRHD;
    }

    if((long)(bp = mem_sbrk(size)) == -1){  // If error in extending heap space return null
        return NULL;
    }

    PUT(HDRP(bp), PACK(size, 0));   // Put the free block header
    PUT(FTRP(bp), PACK(size, 0));   // Put the free block footer
    PUT(HDRP(NEXT_BLKP(bp)), PACK(0, 1));   // Put the new epilogue header

    return coalesce(bp);    // Coalesce if the previous block was free and add the block to the free list
}

/**
 * coalesce Combines the newly freed block with the adjacent free blocks if any
 * bp = The block pointer to the newly freed block
 * returns The pointer to the coalesced block
 */
static void *coalesce(void *bp){
    size_t previous_alloc = GET_ALLOC(FTRP(PREV_BLKP(bp))) || PREV_BLKP(bp) == bp;  // Stores whether the previous block is allocated or not
    size_t next__alloc = GET_ALLOC(HDRP(NEXT_BLKP(bp)));    // Stores whether the next block is allocated or not
    size_t size = GET_SIZE(HDRP(bp));   // Stores the size of the block

    if(previous_alloc && !next__alloc){ // Case 1: The block next to the current block is free
        size += GET_SIZE(HDRP(NEXT_BLKP(bp)));  // Add the size of the next block to the current block to make it a single block
        pop_block(NEXT_BLKP(bp));   // Remove the next block
        PUT(HDRP(bp), PACK(size, 0));   // Update the new block's header
        PUT(FTRP(bp), PACK(size, 0));   // Update the new block's footer
    }

    else if(!previous_alloc && next__alloc){    // Case 2: The block previous to the current block is free
        size += GET_SIZE(HDRP(PREV_BLKP(bp)));  // Add the size of the previous block to the current bloxk to make it a single block
        bp = PREV_BLKP(bp); // Update the block pointer to the previous block
        pop_block(bp);  // Remove the previous block
        PUT(HDRP(bp), PACK(size, 0));   // Update the new block's header
        PUT(FTRP(bp), PACK(size, 0));   // Update the new block's footer
    }

    else if(!previous_alloc && !next__alloc){   // Case 3: The blocks to the either side of the current block are free
        size += GET_SIZE(HDRP(PREV_BLKP(bp))) + GET_SIZE(HDRP(NEXT_BLKP(bp)));  // Add the size of previous and next blocks to the current block to make it single
        pop_block(PREV_BLKP(bp));   // Remove the block previous to the current block
        pop_block(NEXT_BLKP(bp));   // Remove the block next to the current block
        bp = PREV_BLKP(bp); // Update the block pointer to the previous block
        PUT(HDRP(bp), PACK(size, 0));   // Update the new block's header
        PUT(FTRP(bp), PACK(size, 0));   // Update the new block's footer
    }
    push_block(bp); // Insert the block to the start of free list
    return bp;
}

/**
 * push_block = Inserts a block at the front of the free list
 * bp =The pointer of the block to be added at the front of the free list
 */
static void push_block(void *bp){
    NEXT_FREE_PTR(bp) = free_list_ptr; // Sets the next pointer to the start of the free list
    PREV_FREE_PTR(free_list_ptr) = bp;   // Sets the current's previous to the new block
    PREV_FREE_PTR(bp) = NULL; // Set the previosu free pointer to NULL
    free_list_ptr = bp;    // Sets the start of the free list as the new block
}

/**
 * pop_block = Removes a block from the free list
 */
static void pop_block(void *bp){
    if(PREV_FREE_PTR(bp)){    // If there is a previous block
        NEXT_FREE_PTR(PREV_FREE_PTR(bp)) = NEXT_FREE_PTR(bp); // Set the next pointer of the previous block to next block
    }

    else{   // If there is no previous block
        free_list_ptr = NEXT_FREE_PTR(bp); // Set the free list to the next block
    }

    PREV_FREE_PTR(NEXT_FREE_PTR(bp)) = PREV_FREE_PTR(bp);   // Set the previous block's pointer of the next block to the previous block
}

/**
 * find_fit = Finds a fit for the block of a given size
 *  size = The size of the block to be fit
 */
static void *find_fit(size_t size){
    void *bp;

    for(bp = free_list_ptr; GET_ALLOC(HDRP(bp)) == 0; bp = NEXT_FREE_PTR(bp)){ // Traverse the entire free list
        if(size <= GET_SIZE(HDRP(bp))){ // If size fits in the available free block
            return bp;  // Return the block pointer
        }
    }

    return NULL;    // If no fit is found return NULL
}

/**
 * Place a block of specified size to start of free block
 * bp = The block pointer to the free block
 * size =  The size of the block to be placed
 */
static void place(void *bp, size_t size){
    size_t totalsize = GET_SIZE(HDRP(bp));  // Get the total size of thefree block

    if((totalsize - size) >= OVRHD){    // If the difference between the total size and requested size is more than OVRHD, split the block
        PUT(HDRP(bp), PACK(size, 1));   // Put the header of the allocated block
        PUT(FTRP(bp), PACK(size, 1));   // Put the footer of the allocated block
        pop_block(bp);  // Remove the allocated block
        bp = NEXT_BLKP(bp); // The block pointer of the free block created by the partition
        PUT(HDRP(bp), PACK(totalsize - size, 0));   // Put the header of the new unallocated block
        PUT(FTRP(bp), PACK(totalsize - size, 0));   // Put the footer of the new unallocated block
        coalesce(bp);   // Coalesce the new free block with the adjacent free blocks
    }

    else{   // If the remaining space is not enough for a free block then donot split the block
        PUT(HDRP(bp), PACK(totalsize, 1));  // Put the header of the block
        PUT(FTRP(bp), PACK(totalsize, 1));  // Put the footer of the block
        pop_block(bp);  // Remove the allocated block
    }
}

/**
 * Checks the heap for inconsistency
 * Returns 0 if consistent, -1 is inconsistent
 */
// int mm_check(void){
//     void *bp = heap_list_ptr;  // Points to the first block in the heap
//     printf("Heap (%p): \n", heap_list_ptr);    // Print the address of the heap

//     if((GET_SIZE(HDRP(heap_list_ptr)) != OVRHD) || !GET_ALLOC(HDRP(heap_list_ptr))){  // If the first block's header's size or allocated bit is wrong
//         printf("Fatal: Bad prologue header\n"); // Throw error
//         return -1;
//     }
//     if(check_block(heap_list_ptr) == -1){  // Check the block for consistency
//         return -1;
//     }

//     for(bp = free_list_ptr; GET_ALLOC(HDRP(bp)) == 0; bp = NEXT_FREE_PTR(bp)){ // Check all the blocks of free list for consistency
//          if(check_block(bp) == -1){ // If inconsistency
//                 return -1;  // Throw error
//          }
//     }

//     return 0;   // No inconsistency
// }

// /**
//  * Checks a block for consistency
//  * Returns 0 if consistent, -1 if inconsistent
//  */
// static int check_block(void *bp){
//     if(NEXT_FREE_PTR(bp) < mem_heap_lo() || NEXT_FREE_PTR(bp) > mem_heap_hi()){ // If next free pointer is out of heap range
//         printf("Fatal: Next free pointer %p is out of bounds\n", NEXT_FREE_PTR(bp));    // Throw error
//         return -1;
//     }

//     if(PREV_FREE_PTR(bp) < mem_heap_lo() || PREV_FREE_PTR(bp) > mem_heap_hi()){ // If previous free pointer is out of heap range
//         printf("Fatal: Previous free pointer %p is out of bounds", PREV_FREE_PTR(bp));    // Throw error
//         return -1;
//     }

//     if((size_t)bp % 8){ // If there is no alignment
//         printf("Fatal: %p is not aligned", bp); // Throw error
//         return -1;
//     }

//     if(GET(HDRP(bp)) != GET(FTRP(bp))){ // If header and footer mismatch
//         printf("Fatal: Header and footer mismatch");    // Throw error
//         return -1;
//     }

//     return 0;   // Block is consistent
// }