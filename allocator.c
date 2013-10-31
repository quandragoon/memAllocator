/**
 * Copyright (c) 2012 MIT License by 6.172 Staff
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 **/

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "./allocator_interface.h"
#include "./memlib.h"
#include <assert.h>

// Don't call libc malloc!
#define malloc(...) (USE_MY_MALLOC)
#define free(...) (USE_MY_FREE)
#define realloc(...) (USE_MY_REALLOC)


#ifndef MIN_SIZE
  #if (TRACE_CLASS == 0)
    #define MIN_SIZE 1
  #elif (TRACE_CLASS == 2)
    #define MIN_SIZE 8
  #elif (TRACE_CLASS == 4)
    #define MIN_SIZE 32
  #elif (TRACE_CLASS == 5)
    #define MIN_SIZE 8
  #elif (TRACE_CLASS == 7)
    #define MIN_SIZE 512
  #elif (TRACE_CLASS == 8)
    #define MIN_SIZE 4
  #else
    #define MIN_SIZE 64
  #endif
#endif


#ifndef MIN_DIFF
  #if (TRACE_CLASS == 0)
    #define MIN_DIFF 4
  #elif (TRACE_CLASS == 2)
    #define MIN_DIFF 32
  #elif (TRACE_CLASS == 4)
    #define MIN_DIFF 65536
  #elif (TRACE_CLASS == 5)
    #define MIN_DIFF 512
  #elif (TRACE_CLASS == 7)
    #define MIN_DIFF 65536
  #elif (TRACE_CLASS == 8)
    #define MIN_DIFF 2
  #else
    #define MIN_DIFF 128
  #endif
#endif

#ifndef MAX_DIFF
  #if (TRACE_CLASS == 0)
    #define MAX_DIFF 2048
  #elif (TRACE_CLASS == 2)
    #define MAX_DIFF 16384
  #elif (TARCE_CLASS == 5)
    #define MAX_DIFF 32768
  #elif (TRACE_CLASS == 7)
    #define MAX_DIFF 65536
  #elif (TRACE_CLASS == 8)
    #define MAX_DIFF 4096
  #else
    #define MAX_DIFF 512
  #endif
#endif

// #define MIN_BLOCK_SIZE 4

// All blocks must have a specified minimum alignment.
// The alignment requirement (from config.h) is >= 8 bytes.
#ifndef ALIGNMENT
#define ALIGNMENT 8
#endif

// Trace classes are numbered 0..10. Use default value of -1.
#ifndef TRACE_CLASS
#define TRACE_CLASS -1
#endif

// Rounds up to the nearest multiple of ALIGNMENT.
#define ALIGN(size) (((size) + (ALIGNMENT-1)) & ~(ALIGNMENT-1))

// The size of a size_t in bits (approx).
#define SIZE_T_SIZE 26
// #define SIZE_T_SIZE sizeof(size_t)*8

struct free_list {
  struct free_list* next;
  size_t size;
  // short free;
};

typedef struct free_list Free_List;

struct footer {
  size_t size;
  // Free_List* next; 
};
typedef struct footer Footer;
// The smallest aligned value that will hold the header for the free list.
#define FREE_LIST_SIZE (ALIGN(sizeof(Free_List)))
#define FOOTER_SIZE (ALIGN(sizeof(Footer)))

Free_List* FreeList[SIZE_T_SIZE];

// check - This checks our invariant that the size_t header before every
// block points to either the beginning of the next block, or the end of the
// heap.
int my_check() {
  char *p;
  char *lo = (char*)mem_heap_lo();
  char *hi = (char*)mem_heap_hi() + 1;
  size_t size = 0;

  p = lo;
  while (lo <= p && p < hi) {
    Free_List *cur = (Free_List*)p;
    size = ALIGN(cur->size);
    p += size;
  }

  if (p != hi) {
    printf("Bad headers did not end at heap_hi!\n");
    printf("heap_lo: %p, heap_hi: %p, size: %lu, p: %p\n", lo, hi, size, p);
    return -1;
  }

  for (int i=0; i < SIZE_T_SIZE; i++) {
    Free_List *this = FreeList[i];
    while (this) {
      if (this->size <= (1 << (i-1)) || this->size > (1 << i)) {
        printf("You seriously suck. Bin %d had a fucked up node\n", i);
        return -1;
      }
      this = this->next;
    }
  }
  return 0;
}

// init - Initialize the malloc package.  Called once before any other
// calls are made.  Since this is a very simple implementation, we just
// return success.
int my_init() {
  for(int i = 0; i < SIZE_T_SIZE; i++)
    FreeList[i] = NULL;
  return 0;
}

// Returns the upper bound of the log in O(lg N) for N-bit num.
// Bit hack attained from Bit Twiddling Hacks page.
static inline size_t log_upper(size_t val) {
  const unsigned int b[] = {0x2, 0xC, 0xF0, 0xFF00, 0xFFFF0000};
  const unsigned int S[] = {1, 2, 4, 8, 16};
  size_t r = 0;
  // Necessary constraint on argument.
  assert (val > 0);
  val--;
  for (int i = 4; i >= 0; i--) {
    if (val & b[i]) {
      val >>= S[i];
      r |= S[i];
    }
  }
  return r+1;
}
// There is a bit hack to do this in lg N ops where
// N is the number of bits of val (lglg(val)).
// Much faster than current version but current
// version is just a placeholder until malloc
// and free are working.


// Split a power of 2 into multiple powers of 2.
// Algorithm mentioned in lecture for fixed power of 2 binning.
// Reference: Lecture 3 slides on bit hacks.
static inline void split (Free_List* cur, size_t size, size_t index)
{
  Free_List* ptr = NULL;
  size_t s = 1 << index;
  while (index > 0 && size != index){
    index--;
    s >>= 1;
    ptr = (Free_List*)((char*)cur + s);
    ptr->size = s;
    ptr->next = FreeList[index];
    FreeList[index] = ptr;
  }
  cur->size = size;
  return;
}

// When binning by ranges with variable size allocated blocks, 
// shaves off extra portion of chunk and frees it
// prior to allocating.
// This is to allocate exactly what the user asked for and conserve
// the extra in the free list bins.
static inline void chunk (Free_List* cur, size_t aligned_size){
  Free_List* chunk = (Free_List*)((char*)cur + aligned_size);
  chunk->size = cur->size - aligned_size;
  cur->size = aligned_size;
  void* ptr = (void*)((char*)chunk + FREE_LIST_SIZE);
  my_free(ptr);
}

static inline size_t max (size_t x, size_t y){
  return (x > y) ? x : y;
}
static inline size_t min (size_t x, size_t y){
  return (x < y) ? x : y;
}

//  malloc - Allocate a block by incrementing the brk pointer.
//  Always allocate a block whose size is a multiple of the alignment.
void * my_malloc(size_t size) {
  // Find the upper bound lg of size to find corresponding bin
  // If bin is not null, we allocate

  size += FREE_LIST_SIZE;
  size_t aligned_size = max(ALIGN(size), MIN_SIZE);
  size_t lg_size = log_upper(aligned_size);
  size_t index = lg_size + 1;

  Free_List *prev, *cur;
  prev = NULL;
  cur = FreeList[lg_size];

  while (cur && cur->size < aligned_size){
    prev = cur;
    cur = cur->next;
  }
  if (cur){
    if(!prev)
      FreeList[lg_size] = cur->next;
    else
      prev->next = cur->next;
    // cur->free = 0;
    return (void*)((char*)cur + FREE_LIST_SIZE);
  }

  while (index < SIZE_T_SIZE){
    if (FreeList[index]){
      Free_List* c = FreeList[index];
      if (c->size - aligned_size > MAX_DIFF)
        break;

      if (c->size - aligned_size > FREE_LIST_SIZE + MIN_DIFF)
        chunk(c, aligned_size);

      FreeList[index] = c->next;
      // c->free = 0;
      return (void*)((char*)c + FREE_LIST_SIZE);
    }
    index++;
  }

  /*
  size += FREE_LIST_SIZE;
  size_t aligned_size = max(ALIGN(size), ALIGN(MIN_SIZE));
  // size_t x = log_upper(aligned_size);
  // size_t lg_size = x ^ ((x ^ MIN_BLOCK_SIZE) & -(x < MIN_BLOCK_SIZE));
  size_t lg_size = log_upper(aligned_size);
  aligned_size = 1 << lg_size;

  Free_List* cur = NULL;
  if (FreeList[lg_size]){
    cur = FreeList[lg_size];
    FreeList[lg_size] = cur->next;
    return (void*)((char*)cur + FREE_LIST_SIZE);
  }

  for (size_t index = lg_size; index < SIZE_T_SIZE; index++){
    if (FreeList[index]){
      cur = FreeList[index];
      if (cur->size - aligned_size > MAX_DIFF)
        break;
      FreeList[index] = cur->next;
      cur->next = NULL;
      if (index > lg_size + 1){
        split (cur, lg_size, index);
      }
      return (void*)((char*)cur + FREE_LIST_SIZE);
    }
  }
  */

  // We allocate a little bit of extra memory so that we can store the
  // size of the block we've allocated.  Take a look at realloc to see
  // one example of a place where this can come in handy.

  // Expands the heap by the given number of bytes and returns a pointer to
  // the newly-allocated area.  This is a slow call, so you will want to
  // make sure you don't wind up calling it on every malloc.
  void *p = mem_sbrk(aligned_size);

  if (p == (void *)-1) {
    // Whoops, an error of some sort occurred.  We return NULL to let
    // the client code know that we weren't able to allocate memory.
    return NULL;
  } else {
    // We store the size of the block we've allocated in the first
    // FREE_LIST_SIZE bytes.
    ((Free_List*)p)->next = NULL;
    // ((Free_List*)p)->free = 0;
    ((Free_List*)p)->size = aligned_size;
    //TODO: set footer

    // Then, we return a pointer to the rest of the block of memory,
    // which is at least size bytes long.  We have to cast to uint8_t
    // before we try any pointer arithmetic because voids have no size
    // and so the compiler doesn't know how far to move the pointer.
    // Since a uint8_t is always one byte, adding FREE_LIST_SIZE after
    // casting advances the pointer by FREE_LIST_SIZE bytes.
    return (void *)((char *)p + FREE_LIST_SIZE);
  }
}

/*
// Takes a free list pointer and checks whether it can be 
// combined (coalesced) with the next contiguous block.
// TODO: Can be optimized with reverse list pointers,
// so traversal of linked lists is fast.
void coalesce_fwd(Free_List *first) {
  // Need to do a heap boundary check here
  char *boundary = (char*)mem_heap_hi() + 1;
  // If this is the last block in allocated mem, we return.
  if (boundary == ((char*) first + first->size) ) {
    return;
  }
  Free_List *find = (Free_List*)((char*) first + first->size);
  // do ops if only the next contiguous block is free.
  if (find->free) {
    size_t bin = log_upper(find->size);
    Free_List *cur = FreeList[bin];
    Free_List *prev = NULL;
    // Iterate through bin list to remove
    // 'find' from the free list.
    // We want to remove it from the Free List
    // to ensure that we do not allocate this 
    // piece twice (since we plan on coalescing it
    // into an allocated piece now).
    while (cur != find) {
      // should be able to find item
      assert (cur != NULL);
      prev = cur;
      cur = cur->next;
    }
    // edge case if prev is NULL
    if (prev) {
      prev->next = cur->next;
    }
    else {
      FreeList[bin] = cur->next;
    }
    // Effectively coalescing two pieces into the first.
    find->free = 0;
    first->size += find->size; 
  }
}
*/

// free - Find the appropriate bin for a freed
// element by accessing its header and finding
// its size.
// Places size in bin k such that 2^(k-1) < size <= 2^k.
void my_free(void *ptr) {
  Free_List* cur = (Free_List*)((char*)ptr - FREE_LIST_SIZE);
  size_t index = log_upper(cur->size);

  /*
  Free_List* p = NULL;
  Free_List* c = FreeList[index];

  while(c && c->size > cur->size){
    p = c;
    c = c->next;
  }

  if (!p){
    cur->next = c;
    FreeList[index] = cur;
  }
  else{
    cur->next = c;
    p->next = cur;
  }
  */
  
  cur->next = FreeList[index];
  // cur->free = 1;
  FreeList[index] = cur;
  
  // Want to place freed block in a bin
  // that will allow it to be sufficient
  // for any future query of that size (2^k).
  // In other words, put in kth bin if
  // 2^k < size <= 2^(k+1)
  // NOTE: Do a power of two check here
  // to prevent putting 2^k in k-1 bin.
  // assert((log_upper(size) > 0) && (log_upper(size) < SIZE_T_SIZE));
  // Append to front of free list
}

// realloc - Implemented simply in terms of malloc and free
void * my_realloc(void *ptr, size_t size) {
  void *newptr;
  size_t copy_size;

  if(!ptr)
    return my_malloc(size);
  
  if (size == 0){
    my_free(ptr);
    return NULL;
  }


  size_t aligned_size = ALIGN(size + FREE_LIST_SIZE);
  // Here is the header we are working with.
  Free_List* mem = (Free_List*)((char*)ptr - FREE_LIST_SIZE);
  copy_size = min(mem->size - FREE_LIST_SIZE, ALIGN(size));


  // If the new block is smaller than the old one, we have to stop copying
  // early so that we don't write off the end of the new block of memory.
  if (aligned_size + MIN_DIFF < copy_size){
  // if (2*aligned_size <= copy_size){
    chunk(mem, aligned_size);
    return ptr;
  }

  // Didn't shave off chunk, but still should return ptr
  // since new block smaller than old one.
  if (mem->size >= aligned_size)
    return ptr;

  // for consecutive, increasing reallocs
  if ((char*)mem + mem->size == my_heap_hi()+1)
  {
    mem_sbrk(aligned_size - mem->size);
    mem->size = aligned_size;
    return ptr;
  }

  // Allocate a new chunk of memory, and fail if that allocation fails.
  newptr = my_malloc(size);
  if (NULL == newptr)
    return NULL;

  // Get the size of the old block of memory.  Take a peek at my_malloc(),
  // where we stashed this in the FREE_LIST_SIZE bytes directly before the
  // address we returned.  Now we can back up by that many bytes and read
  // the size by pulling it from the free list header.

  // This is a standard library call that performs a simple memory copy.
  // We only want to copy the original size of mem so that's why we saved it.
  memcpy(newptr, ptr, copy_size);

  // Release the old block.
  my_free(ptr);

  // Return a pointer to the new block.
  return newptr;
}

// call mem_reset_brk.
void my_reset_brk() {
  mem_reset_brk();
}

// call mem_heap_lo
void * my_heap_lo() {
  return mem_heap_lo();
}

// call mem_heap_hi
void * my_heap_hi() {
  return mem_heap_hi();
}
