/*
 * mm-naive.c - The fastest, least memory-efficient malloc package.
 * 
 * In this naive approach, a block is allocated by simply incrementing
 * the brk pointer.  A block is pure payload. There are no headers or
 * footers.  Blocks are never coalesced or reused. Realloc is
 * implemented directly using mm_malloc and mm_free.
 *
 * NOTE TO STUDENTS: Replace this header comment with your own header
 * comment that gives a high level description of your solution.
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
    "STEAM",
    /* First member's full name */
    "Lucas",
    /* First member's email address */
    "2018202150@ruc.edu.cn",
    /* Second member's full name (leave blank if none) */
    "",
    /* Second member's email address (leave blank if none) */
    ""
};

/* single word (4) or double word (8) alignment */
#define ALIGNMENT 8

/* rounds up to the nearest multiple of ALIGNMENT */
#define ALIGN(size) (((size) + (ALIGNMENT-1)) & ~0x7)


#define SIZE_T_SIZE (ALIGN(sizeof(size_t)))

#define CHUNK 1<<12
#define WSIZE 4
#define DSIZE 8
#define MAX(x,y) ((x)>(y)?(x):(y))

#define PACK(size,alloc) ((size)|(alloc))
#define GET(p) (*(unsigned int*)(p))//get 4 bytes;
#define PUT(p,val) (*(unsigned int*)(p)=(unsigned int)(val))//write 4 bytes

#define GET_SIZE(p) (GET(p) & ~0x7)
#define GET_ALLOC(p) (GET(p)& 0x1)

#define HDPR(bp) ((char*)(bp)-WSIZE)
#define FTPR(bp) ((char*)(bp)+GET_SIZE(HDPR(bp))-DSIZE)
#define NEXT_BLKP(bp) ((char*)(bp)+GET_SIZE(((char*)(bp)-WSIZE)))
#define PREV_BLKP(bp) ((char*)(bp)-GET_SIZE(((char*)(bp)-DSIZE)))
#define NEXT_PTR(bp) ((void*)((char*)(bp)+WSIZE))
#define PREV_PTR(bp) ((void*)bp)
#define NEXT(bp) (*(void**)(NEXT_PTR(bp)))
#define PREV(bp) (*(void**)(PREV_PTR(bp)))


void* seekroot(size_t size)
{
    if(size<=32)
        return (char*)mem_heap_lo()+1*WSIZE;
    else if (size<=64) 
        return (char*)mem_heap_lo()+2*WSIZE;
    else if(size<=128)
        return (char*)mem_heap_lo()+3*WSIZE;
    else if(size<=512)
        return (char*)mem_heap_lo()+4*WSIZE;
    else if(size<=1024)
        return (char*)mem_heap_lo()+5*WSIZE;
    else if(size<=2048)
        return (char*)mem_heap_lo()+6*WSIZE;
    else if(size<=4096)
        return (char*)mem_heap_lo()+7*WSIZE;
    else
    {
        return (char*)mem_heap_lo()+8*WSIZE;
    }
}

void add(void* bp,size_t size)
{
    void* root_ptr=seekroot(size);
    if(*(void**)root_ptr==NULL)
    {
        PUT(root_ptr,bp);
        PUT(NEXT_PTR(bp),NULL);
        PUT(PREV_PTR(bp),NULL);
        return;
    }
    void* now= *(void **)root_ptr;
    if(size<=GET_SIZE(HDPR(now)))
    {
        PUT(NEXT_PTR(bp),now);
        PUT(PREV_PTR(now),bp);
        PUT(PREV_PTR(bp),NULL);
        PUT(root_ptr,bp);
        return;
    }
    for(;GET_SIZE(HDPR(now))>0 && NEXT(now)!=NULL;now=NEXT(now))
    {
        if(GET_SIZE(HDPR(now))<=size && GET_SIZE(HDPR(NEXT(now)))>=size)
        {
            PUT(NEXT_PTR(bp),NEXT(now));
            PUT(PREV_PTR(bp),now);
            PUT(PREV_PTR(NEXT(bp)),bp);
            PUT(NEXT_PTR(PREV(bp)),bp);
            return;
        }
    }
    if(NEXT(now)==NULL)
    {
        PUT(PREV_PTR(bp),now);
        PUT(NEXT_PTR(bp),NULL);
        PUT(NEXT_PTR(now),bp);
        return;
    }
}
void del(void*bp, size_t size)
{
    void* root_ptr=seekroot(size);
    if(NEXT(bp)==NULL && PREV(bp)!=NULL)
    {
        PUT(NEXT_PTR(PREV(bp)),NULL);
        PUT(NEXT_PTR(bp),NULL);
        PUT(PREV_PTR(bp),NULL);

    }
    else if(PREV(bp)==NULL && NEXT(bp)!=NULL)
    {
        PUT(PREV_PTR(NEXT(bp)),NULL);
        PUT(root_ptr,NEXT(bp));
        PUT(NEXT_PTR(bp),NULL);
        PUT(PREV_PTR(bp),NULL);
    }
    else if(PREV(bp)!=NULL && NEXT(bp)!=NULL)
    {
        PUT(PREV_PTR(NEXT(bp)),PREV(bp));
        PUT(NEXT_PTR(PREV(bp)),NEXT(bp));
    }
    else if(PREV(bp)==NULL && NEXT(bp)==NULL)
    {
        
        PUT(root_ptr,NULL);
    }


}
void* coalesce(void*bp)
{
    
    size_t prev_alloc=GET_ALLOC(FTPR(PREV_BLKP(bp)));
    size_t next_alloc=GET_ALLOC(HDPR(NEXT_BLKP(bp)));
    size_t size=GET_SIZE(HDPR(bp));
    if(prev_alloc && next_alloc)
    {
        add(bp,size);
        return  bp;
    }   
    else if(prev_alloc && !next_alloc)
    {
        size+=GET_SIZE(HDPR(NEXT_BLKP(bp)));
        del(NEXT_BLKP(bp),GET_SIZE(HDPR(NEXT_BLKP(bp))));
        PUT((HDPR(bp)),PACK(size,0));
        PUT(FTPR(bp),PACK(size,0));
        add(bp,size); 
    }
    else if(!prev_alloc && next_alloc)
    {
        size+=GET_SIZE(HDPR(PREV_BLKP(bp)));
        del(PREV_BLKP(bp),GET_SIZE(HDPR(PREV_BLKP(bp))));
        PUT(FTPR(bp),PACK(size,0));
        PUT(HDPR(PREV_BLKP(bp)),PACK(size,0));
        bp=PREV_BLKP(bp);
        add(bp,size);
    }
    else
    {
        size+=GET_SIZE(HDPR(PREV_BLKP(bp)));
        size+=GET_SIZE(FTPR(NEXT_BLKP(bp)));
        del(NEXT_BLKP(bp),GET_SIZE(HDPR(NEXT_BLKP(bp))));
        del(PREV_BLKP(bp),GET_SIZE(HDPR(PREV_BLKP(bp))));
        PUT(FTPR(NEXT_BLKP(bp)),PACK(size,0));
        PUT(HDPR(PREV_BLKP(bp)),PACK(size,0));
        bp=PREV_BLKP(bp);
        add(bp,size);
    }
    return bp;
}
void* extend_heap(size_t words)
{
    void* bp;
    size_t size;
    size= (words%2)? (words+1)*WSIZE:(words*WSIZE);
    if((bp=mem_sbrk(size))==(void*)(-1))
        return NULL;
    PUT(HDPR(bp),PACK(size,0));
    PUT(NEXT_PTR(bp),NULL);
    PUT(PREV_PTR(bp),NULL);
    PUT(FTPR(bp),PACK(size,0));
    PUT(HDPR(NEXT_BLKP(bp)),PACK(0,1));
    return coalesce(bp);
}




/* 
 * mm_init - initialize the malloc package.
 */
int mm_init(void)
{
    mem_sbrk(12*WSIZE);
    if(mem_heap_lo()==(void*)-1)
        return 1;
    PUT(mem_heap_lo(),0);
    for(int i=1;i<=8;i++)
        PUT((char*)mem_heap_lo()+WSIZE*i,NULL);
    PUT((char*)mem_heap_lo()+9*WSIZE,PACK(8,1));
    PUT((char*)mem_heap_lo()+10*WSIZE,PACK(8,1));
    PUT((char*)mem_heap_lo()+11*WSIZE,PACK(0,1));
    if(extend_heap(CHUNK/WSIZE)==NULL)
        return 1;
    return 0;
    
}

/* 
 * mm_malloc - Allocate a block by incrementing the brk pointer.
 *     Always allocate a block whose size is a multiple of the alignment.
 */
/* find_fit first fit version
void* find_fit(size_t asize)
{
    void* heap_listp=(void*)((char*)(mem_heap_lo())+2*WSIZE);
    void* rover;
    for(rover=heap_listp;GET_SIZE(HDPR(rover))>0;rover=NEXT_BLKP(rover))
        if(GET_ALLOC(HDPR(rover))==0&&(asize<GET_SIZE(HDPR(rover))))
            return rover;
    return NULL;
}
*/
/* find_fit implicit list best fit version
void* find_fit(size_t asize)
{
    void* heap_listp=(void*)((char*)(mem_heap_lo())+2*WSIZE);
    void* rover;
    void* best_bp=(void*)-1;
    unsigned residue=32768;
    for(rover=heap_listp;GET_SIZE(HDPR(rover))>0;rover=NEXT_BLKP(rover))
        if(GET_ALLOC(HDPR(rover))==0&&(asize<GET_SIZE(HDPR(rover))))
        {
            if(GET_SIZE(HDPR(rover))-asize<residue)
            {
                residue=GET_SIZE(HDPR(rover))-asize;
                best_bp=rover;
            }
        }
    if(best_bp==(void*)-1)
        return NULL;
    return best_bp;
}
*/
void* find_fit(size_t asize)
{
    void* root_ptr=seekroot(asize);
    for(;root_ptr!=(char*)mem_heap_lo()+9*WSIZE;root_ptr+=WSIZE)
    {
        void* now;
        now=*(void**)root_ptr;
        while(now!=NULL)
        {
            if(GET_SIZE(HDPR(now))>=asize)
                return now;
            now=NEXT(now);
        }
    }
    return NULL;
}
void place(void*bp,size_t asize)
{
    //asize is already alligned.
    size_t oldsize=GET_SIZE(HDPR(bp));
    size_t newsize=asize;
    
    if((int)(oldsize-newsize)<16)
    {
        PUT(HDPR(bp),PACK(oldsize,1));
        PUT(FTPR(bp),PACK(oldsize,1)); 
        del(bp,oldsize);
    }
    else
    {
        del(bp,oldsize);
        PUT(HDPR(bp),PACK(newsize,1));
        PUT(FTPR(bp),PACK(newsize,1));
        bp=NEXT_BLKP(bp);
        PUT(HDPR(bp),PACK(oldsize-newsize,0));
        PUT(FTPR(bp),PACK(oldsize-newsize,0));
        PUT(NEXT_PTR(bp),NULL);
        PUT(PREV_PTR(bp),NULL);
        coalesce(bp);
    }
}
void *mm_malloc(size_t size)
{
    size_t asize;
    size_t extendsize;
    void *bp;
    
    if(size==0)
        return NULL;
    if(size<=DSIZE)
        asize=2*DSIZE;
    else
        asize=DSIZE*((size+(DSIZE)+(DSIZE-1))/DSIZE);
    if((bp=find_fit(asize))!=NULL)
    {
        place(bp,asize);
        //printf("%p",bp);
        return bp;
    }
    extendsize=asize>CHUNK? asize:CHUNK;
    if((bp=extend_heap(extendsize/WSIZE))==NULL)
        return NULL;
    place(bp,asize);
    return bp;

}

/*
 * mm_free - Freeing a block does nothing.
 */
void mm_free(void *bp)
{
    size_t size=GET_SIZE(HDPR(bp));
    PUT(HDPR(bp),PACK(size, 0));
    PUT(FTPR(bp),PACK(size, 0));
    PUT(NEXT_PTR(bp),NULL);
    PUT(PREV_PTR(bp),NULL);

    coalesce(bp);

    
}

/*
 * mm_realloc - Implemented simply in terms of mm_malloc and mm_free
 */
/*
void *mm_realloc(void *ptr, size_t size)
{
    void *oldptr = ptr;
    void *newptr;
    size_t copySize;
    
    newptr = mm_malloc(size);
    if (newptr == NULL)
        return NULL;
    copySize = GET_SIZE(HDPR(oldptr));
    if (size < copySize)
        copySize = size;
    memcpy(newptr, oldptr, copySize);
    mm_free(oldptr);
    return newptr;
}
*/
void* r_coalesce(void*bp)
{
    size_t prev_alloc=GET_ALLOC(HDPR(PREV_BLKP(bp)));
    size_t next_alloc=GET_ALLOC(HDPR(NEXT_BLKP(bp)));
    size_t size=GET_SIZE(HDPR(bp));
    if(prev_alloc && next_alloc)
        return bp;
    else if(prev_alloc && ! next_alloc)
    {
        size+=GET_SIZE(HDPR(NEXT_BLKP(bp)));
        del(NEXT_BLKP(bp),GET_SIZE(HDPR(NEXT_BLKP(bp))));
        PUT(HDPR(bp),PACK(size,1));
        PUT(FTPR(bp),PACK(size,1));
    }
    else if(!prev_alloc && next_alloc)
    {
        size+=GET_SIZE(HDPR(PREV_BLKP(bp)));
        del(PREV_BLKP(bp),GET_SIZE(HDPR(PREV_BLKP(bp))));
        bp=PREV_BLKP(bp);
        PUT(HDPR(bp),PACK(size,1));
        PUT(FTPR(bp),PACK(size,1));
    }
    else
    {
        size+=GET_SIZE(HDPR(NEXT_BLKP(bp)));
        size+=GET_SIZE(HDPR(PREV_BLKP(bp)));
        del(PREV_BLKP(bp),GET_SIZE(HDPR(PREV_BLKP(bp))));
        del(NEXT_BLKP(bp),GET_SIZE(HDPR(NEXT_BLKP(bp))));
        bp=PREV_BLKP(bp);
        PUT(HDPR(bp),PACK(size,1));
        PUT(FTPR(bp),PACK(size,1));
        
    }
    return bp;
    
}
void *mm_realloc(void*bp,size_t newsize)
{
    if(bp==NULL)
        return mm_malloc(newsize);
    if(newsize==0)
    {
        mm_free(bp);
        return NULL;
    }
    size_t oldsize=GET_SIZE(HDPR(bp));
    if(newsize<2*DSIZE)
        newsize=2*DSIZE;
    else
        newsize=DSIZE*((newsize+DSIZE+DSIZE-1)/DSIZE);
    void* newptr;
    if(newsize<oldsize)
    {
        return bp;
    }
     
       
    void* oldptr;
    oldptr=r_coalesce(bp);
    if(GET_SIZE(HDPR(oldptr))>=newsize)
    {
        memcpy(oldptr,bp,oldsize);
        return oldptr;
    }
    else
    {
        newptr=mm_malloc(newsize);
        if(newptr==NULL)
            return NULL;
        memcpy(newptr,bp,oldsize);
        mm_free(bp);
        return newptr;
    }
    
    


}

















