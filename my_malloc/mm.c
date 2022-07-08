#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <string.h>

#include "mm.h"
#include "memlib.h"

/*********************************************************
 * NOTE TO STUDENTS: Before you do anything else, please
 * provide your information in the following struct.
 ********************************************************/
team_t team = {
    /* Your student ID */
    "20181621",
    /* Your full name*/
    "Hyunchul Kim",
    /* Your email address */
    "rlaguscjf0902@gmail.com",
};

/* Basic constants and macros referenced from the textbook */
/* $begin mallocmacros */
#define WSIZE       4       /* Word and header/footer size (bytes) */
#define DSIZE       8       /* Doubleword size (bytes) */
#define CHUNKSIZE  (1<<12)  /* Extend heap by this amount (bytes) */
#define NSEGS       12      /* Number of segregated lists */

#define MAX(x, y) ((x) > (y)? (x) : (y))  

/* Pack a size and allocated bit into a word */
#define PACK(size, alloc)  ((size) | (alloc)) 

/* Read and write a word at address p */
#define GET(p)       (*(unsigned int *)(p))            
#define PUT(p, val)  (*(unsigned int *)(p) = (val))

/* Read next/previous block's pointer next to block p*/
#define GET_PRV(p)   (*((unsigned int *)(p))+WSIZE) 
#define GET_NXT(p)   (*((unsigned int *)(p))-WSIZE) 

/* Read and write an address(double word) at address p */
#define GET2W(p)       ((char *) *(unsigned int **)(p))
#define PUT2W(p, val)  (*(unsigned int **)(p) = (unsigned int *)(val))

/* Read the size and allocated fields from address p */
#define GET_SIZE(p)  (GET(p) & ~0x7)                   
#define GET_ALLOC(p) (GET(p) & 0x1)                

/* Given block ptr bp, compute address of its header and footer */
#define HDRP(bp)       ((char *)(bp) - WSIZE)                      
#define FTRP(bp)       ((char *)(bp) + GET_SIZE(HDRP(bp)) - DSIZE) 

/* Given block ptr bp, compute address of next and previous blocks */
#define NEXT_BLKP(bp)  ((char *)(bp) + GET_SIZE(((char *)(bp) - WSIZE))) 
#define PREV_BLKP(bp)  ((char *)(bp) - GET_SIZE(((char *)(bp) - DSIZE))) 

/* Given size of a block, compute the class that the block belongs to */
#define GET_SEG_CLASS(X) (32 - __builtin_clz(X-1) - 4 < NSEGS) ? (32 - __builtin_clz(X-1) - 4) : (NSEGS -1)

/* get truly allocated size including boundary tags and alignment padding*/
#define ALLOC_SIZE(X) (X%DSIZE) ? ((X/DSIZE + 2) * DSIZE) : (X+DSIZE)
/* $end mallocmacros */

/* Global variables */ 
static char *heap_listp = 0;  /* Pointer to first block */
static char *epilogue_address = 0; /* Pointer to last block */

/* Function prototypes */
int mm_init(void);
void *mm_malloc(size_t size);
void mm_free(void *ptr);
void *mm_realloc(void *ptr, size_t size);
int mm_check(void);
static void *extend_heap(size_t size);
static void *find_fit(size_t asize);
static void *coalesce(void *bp);
static void add_to_seg_list(void *bp);
static void remove_from_seg_list(void *bp);

/* 
 * mm_init - initialize the malloc package.
 */
int mm_init(void)
{
    /* Create the initial empty heap */
    if ((heap_listp = mem_sbrk(4*WSIZE+NSEGS*DSIZE)) == (void *)-1)
	    return -1;
    PUT(heap_listp, 0);                                    /* Alignment padding */
    PUT(heap_listp + (1*WSIZE), PACK(DSIZE*(NSEGS+1), 1)); /* Prologue header */ 
    PUT(heap_listp + (2*WSIZE+NSEGS*DSIZE), PACK(DSIZE*(NSEGS+1), 1)); /* Prologue footer */ 
    PUT(heap_listp + (3*WSIZE+NSEGS*DSIZE), PACK(0, 1));   /* Epilogue header */
    
    heap_listp += (2*WSIZE);                     
    epilogue_address = heap_listp + (WSIZE+NSEGS*DSIZE);

    /* Initialize segregated classes' pointer to NULL */
    for(int i=0;i<NSEGS;i++)
        PUT2W(heap_listp + i*DSIZE, 0);

    if (extend_heap(CHUNKSIZE) == NULL)
        return -1;

    return 0;
}

/* 
 * mm_malloc - allocate memory given the size
 */
void *mm_malloc(size_t size)
{
    size_t asize = ALLOC_SIZE(size);
    size_t left_blk_size, extend_size;
    char *bp;
    char *left_blk;

    /* Find a appropriate free block in segregated classes*/
    bp = find_fit(asize);

    /* If none found, extend heap */
    if (!bp)
    {
        extend_size = asize;
        if (!GET_ALLOC(epilogue_address - WSIZE))
            extend_size -= GET_SIZE(epilogue_address - WSIZE);

        extend_size = MAX(extend_size, CHUNKSIZE);
        bp = extend_heap(extend_size);
    }
    
    remove_from_seg_list(bp);

    if (GET_SIZE(HDRP(bp)) < asize + 16) // If leftover smaller than the minimum size(16 byte) of a free block,
        asize = GET_SIZE(HDRP(bp));      // extend allocated size with no leftover

    left_blk_size = GET_SIZE(HDRP(bp));
    left_blk_size -= asize;

    PUT(HDRP(bp), PACK(asize, 1));          
    PUT(FTRP(bp), PACK(asize, 1));         

    /* Insert leftover to segregated list */
    if (left_blk_size)
    {
        left_blk = NEXT_BLKP(bp);
        PUT(HDRP(left_blk), PACK(left_blk_size, 0));          
        PUT(FTRP(left_blk), PACK(left_blk_size, 0)); 
        add_to_seg_list(left_blk);
    }
    return bp;
}

/*
 * mm_free - Free a block
 */
void mm_free(void *ptr)
{
    if (ptr == 0)
        return;

    size_t size = GET_SIZE(HDRP(ptr));

    PUT(HDRP(ptr), PACK(size, 0));
    PUT(FTRP(ptr), PACK(size, 0));

    coalesce(ptr);
}

/*
 * mm_realloc - reallocate a given block
 */
void *mm_realloc(void *ptr, size_t size)
{
    size_t oldsize = GET_SIZE(HDRP(ptr));
    size_t asize;
    void *newptr;

    if (size == 0)
    {
        mm_free(ptr);
        return 0;
    }

    if (ptr == NULL)
        return mm_malloc(size);

    asize = ALLOC_SIZE(size);

    /* 
       If requested size with additional memory exceeds existing block, 
       free and re-allocate
    */
    if (asize > oldsize)
    {
        newptr = mm_malloc(size);
        memcpy(newptr, ptr, oldsize);
        mm_free(ptr);
        return newptr;
    }
    
    return ptr;
}

/*
 * mm_check - Check if heap is well managed
 */
int mm_check(void)
{
    char *ptr, *temp;

    /* check if blocks are not overlapped */
    ptr = heap_listp;
    while (ptr < epilogue_address)
    {
        temp = (char*)NEXT_BLKP(ptr);
        if (GET_SIZE(ptr) != temp - ptr)
            return 0;
        ptr = temp;
    }

    for (int i = 0; i < NSEGS; i++)
    {
        ptr = (char*)GET(heap_listp + i*DSIZE);
        while (ptr)
        {
            // check if free block marked as free
            if (GET_ALLOC(ptr))
                return 0;
            // check if neighboring blocks are free (not coalesced)
            if (!GET_ALLOC(PREV_BLKP(ptr)) || !GET_ALLOC(NEXT_BLKP(ptr)))
                return 0;
            // check if free blocks in segregated list are well connected
            if (GET(ptr) && ptr != (char*)GET_PRV(ptr))
                return 0;
            ptr = (char*)GET(ptr);
        }
    }

    return 1;
}

/* 
 * extend_heap - Extend heap with free block and return its block pointer
 */
static void *extend_heap(size_t size) 
{
    char *bp;
    size_t newsize;

    /* Allocate an even number of words to maintain alignment */
    newsize = ALLOC_SIZE(size);

    if ((long)(bp = mem_sbrk(newsize)) == -1)  
	    return NULL;

    /* Initialize free block header/footer and the epilogue header */
    PUT(HDRP(bp), PACK(newsize, 0));         /* Free block header */   
    PUT(FTRP(bp), PACK(newsize, 0));         /* Free block footer */   
    PUT(HDRP(NEXT_BLKP(bp)), PACK(0, 1));    /* New epilogue header */ 
    epilogue_address = HDRP(NEXT_BLKP(bp));

    /* Coalesce if the previous block was free */
    return coalesce(bp);                                          
}

/* 
 * find_fit - Find a fit for a block with asize bytes 
 */
static void *find_fit(size_t asize)
{
    char *bp;
    int i;

    for(i=GET_SEG_CLASS(asize); i<NSEGS; i++)
    {
        bp = (char*)GET(heap_listp + i*DSIZE);
        while (bp != 0)
        {
            if (GET_SIZE(HDRP(bp)) >= asize)
                return bp;
            bp = (char*)GET(bp);
        }
    }
    
    return NULL; /* No fit */
}

/* 
 * coalesce - coalesce neighboring free blocks
 */
static void *coalesce(void *bp)
{
    //Coalesce also ensures that free block gets into the correct free list
    size_t prev_alloc = GET_ALLOC(FTRP(PREV_BLKP(bp)));
    size_t next_alloc = GET_ALLOC(HDRP(NEXT_BLKP(bp)));
    size_t size = GET_SIZE(HDRP(bp));


    if (prev_alloc && next_alloc)              /* Case 1 */
    {
        add_to_seg_list(bp);
        return bp;
    }
    else if (prev_alloc && !next_alloc)        /* Case 2 */
    {
        remove_from_seg_list(NEXT_BLKP(bp));
        size += GET_SIZE(HDRP(NEXT_BLKP(bp)));
        PUT(HDRP(bp), PACK(size, 0));
        PUT(FTRP(bp), PACK(size, 0));
        add_to_seg_list(bp);
        return bp;
    }
    else if (!prev_alloc && next_alloc)        /* Case 3 */
    {   
        remove_from_seg_list(PREV_BLKP(bp));
        bp=PREV_BLKP(bp);
        size += GET_SIZE(HDRP(bp));
        PUT(HDRP(bp), PACK(size, 0));
        PUT(FTRP(bp), PACK(size, 0));
        add_to_seg_list(bp);
        return bp;
    }
    else                                       /* Case 4 */
    {
        remove_from_seg_list(NEXT_BLKP(bp));
        remove_from_seg_list(PREV_BLKP(bp));
        size += GET_SIZE(HDRP(NEXT_BLKP(bp)));
  
        bp=PREV_BLKP(bp);
        size += GET_SIZE(HDRP(bp));
        PUT(HDRP(bp), PACK(size, 0));
        PUT(FTRP(bp), PACK(size,0));
        add_to_seg_list(bp);
        return bp;
    }
    return NULL;
}

/* 
 * add_to_seg_list - add free block to segregated list
 */
static void add_to_seg_list(void *bp)
{
    char *classPointer;
    size_t blk_size = GET_SIZE(HDRP(bp));
    int class = GET_SEG_CLASS(blk_size);

    classPointer = heap_listp + DSIZE * class;
    /* Adjust links of blocks */
    if (GET(classPointer) != 0)
        PUT(GET_PRV(classPointer), (int)bp+WSIZE); 
    PUT(bp+WSIZE, (int)classPointer+WSIZE);
    PUT(bp, GET(classPointer));
    PUT(classPointer, (int)bp);

}

/* 
 * remove_from_seg_list - remove free block from segregated list
 */
static void remove_from_seg_list(void *bp)
{
    /* Adjust links of blocks */
    if(GET(bp) != 0)
        PUT(GET_PRV(bp), GET(bp+WSIZE));
    PUT(GET_NXT(bp+WSIZE), GET(bp));
    PUT2W(bp, 0);
}