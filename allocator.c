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

// Largest request will be around 50 MB which is well bounded above by 2^26.
#define SIZE_T_SIZE 26

struct free_list {
  struct free_list* next;
  size_t size;
};

typedef struct free_list Header;

// The smallest aligned value that will hold the header for the free list.
#define HEADER_SIZE (ALIGN(sizeof(Header)))

Header* FreeList[SIZE_T_SIZE];

// check - This checks our invariants.
// 1. All sizes should add up to total space allocated from heap.
// 2. Each bin has chunks of size between 2^(i-1) and 2^i, inclusive
// of the latter.
// 3. Each bin has chunks of increasing size as we progress through (sorted).
int my_check() {
  char *p;
  char *lo = (char*)mem_heap_lo();
  char *hi = (char*)mem_heap_hi() + 1;
  size_t size = 0;

  p = lo;
  while (lo <= p && p < hi) {
    Header *cur = (Header*)p;
    size = ALIGN(cur->size);
    p += size;
  }

  if (p != hi) {
    printf("Bad headers did not end at heap_hi!\n");
    printf("heap_lo: %p, heap_hi: %p, size: %lu, p: %p\n", lo, hi, size, p);
    return -1;
  }


  for (int i=0; i < SIZE_T_SIZE; i++) {
    Header *this = FreeList[i];
    size_t previous = 0;
    while (this) {
      if (this->size <= (1 << (i-1)) || this->size > (1 << i)) {
        printf("Bin %d had a node of incorrect size\n", i);
        return -1;
      }
      if (this->size < previous) {
        printf("Sorted invariant was broken by bin %d\n", i);
        return -1;
      }
      previous = this->size;
      this = this->next;
    }
  }
  return 0;
}

// init - Initialize the malloc package.  Just assigns all bins
// in the FreeList array.
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

// When binning by ranges with variable size allocated blocks, 
// shaves off extra portion of chunk and frees it
// prior to allocating. This is to allocate exactly what the user 
// asked for and conserve the extra in the free list bins.
static inline void chunk (Header* cur, size_t aligned_size){
  Header* chunk = (Header*)((char*)cur + aligned_size);
  chunk->size = cur->size - aligned_size;
  cur->size = aligned_size;
  void* ptr = (void*)((char*)chunk + HEADER_SIZE);
  my_free(ptr);
}

// simple max function
static inline size_t max (size_t x, size_t y){
  return (x > y) ? x : y;
}
// simple min function
static inline size_t min (size_t x, size_t y){
  return (x < y) ? x : y;
}

//  malloc - First check if we can reallocate freed memory
//  by checking the appropriate bins (which are each sorted).
//  We then decide whether we should chunk a contiguous piece
//  or whether its worth freeing an abnormally large piece
//  for a comparatively small request.
//  If we do not pass these checks to allocate freed memory,
//  we increment the brk pointer and extend the heap.
void * my_malloc(size_t size) {

  assert (my_check() == 0);

  size += HEADER_SIZE;
  // Use parameter to define "too small" of a request.
  size_t aligned_size = max(ALIGN(size), MIN_SIZE);
  // Find appropriate bin to start searching.
  size_t lg_size = log_upper(aligned_size);
  size_t index = lg_size + 1;

  Header *prev, *cur;
  prev = NULL;
  cur = FreeList[lg_size];

  // Search for smallest size that fits based on sorted
  // order.
  while (cur && cur->size < aligned_size){
    prev = cur;
    cur = cur->next;
  }
  // If we found an element, we check the boundary
  // case that it may be the first element.
  if (cur){
    if(!prev)
      FreeList[lg_size] = cur->next;
    else
      prev->next = cur->next;
    
    return (void*)((char*)cur + HEADER_SIZE);
  }

  while (index < SIZE_T_SIZE){
    if (FreeList[index]){
      Header* c = FreeList[index];
      // Parameter to determine whether to 
      // break off new heap chunk or not.
      // This is based on whether it's worth allocating
      // a large block to a relatively small malloc
      // request.
      if (c->size - aligned_size > MAX_DIFF)
        break;

      FreeList[index] = c->next;
      // Checks parameter to see whether it's worth chunking
      // 'c' into two pieces based on the aligned_size.
      if (c->size - aligned_size > HEADER_SIZE + MIN_DIFF)
        chunk(c, aligned_size);

      return (void*)((char*)c + HEADER_SIZE);
    }
    index++;
  }

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
    // HEADER_SIZE bytes.
    ((Header*)p)->next = NULL;
    ((Header*)p)->size = aligned_size;

    // Then, we return a pointer to the rest of the block of memory,
    // which is at least size bytes long.  We have to cast to uint8_t
    // before we try any pointer arithmetic because voids have no size
    // and so the compiler doesn't know how far to move the pointer.
    // Since a uint8_t is always one byte, adding HEADER_SIZE after
    // casting advances the pointer by HEADER_SIZE bytes.
    return (void *)((char *)p + HEADER_SIZE);
  }
}


// free - Find the appropriate bin for a freed
// element by accessing its header and finding
// its size.
// Places size in bin k such that 2^(k-1) < size <= 2^k.
// Also, places the chunk in the bin such that the bin
// is still sorted.
void my_free(void *ptr) {
  Header* cur = (Header*)((char*)ptr - HEADER_SIZE);
  size_t index = log_upper(cur->size);
  Header* p = NULL;
  Header* c = FreeList[index];

  // Keep lists sorted.
  while(c && c->size < cur->size){
    p = c;
    c = c->next;
  }

  // Edge case where p is NULL,
  // c becomes the first element of the bin.
  if (!p){
    cur->next = c;
    FreeList[index] = cur;
  }
  else{
    cur->next = c;
    p->next = cur;
  }
  assert (my_check() == 0);
}

// realloc - Implemented simply in terms of malloc and free
void * my_realloc(void *ptr, size_t size) {
  assert (my_check() == 0);
  void *newptr;
  size_t copy_size;

  if(!ptr)
    return my_malloc(size);
  
  if (size == 0){
    my_free(ptr);
    return NULL;
  }

  // Total necessary size including header.
  size_t aligned_size = ALIGN(size + HEADER_SIZE);
  // Here is the header we are working with.
  Header* mem = (Header*)((char*)ptr - HEADER_SIZE);
  // This is the amount we want to copy by.
  copy_size = min(mem->size, aligned_size) - HEADER_SIZE;


  // If the new block is smaller than the old one, we have to stop copying
  // early so that we don't write off the end of the new block of memory.
  if (aligned_size + MIN_DIFF < mem->size){
    chunk(mem, aligned_size);
    return ptr;
  }

  // Didn't shave off chunk, but still should return ptr
  // since new block smaller than old one.
  if (mem->size >= aligned_size)
    return ptr;
  
  // For consecutive, increasing reallocs.
  // If the block being reallocated is at the end of
  // the allocated portion of the heap, just extend it.
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
  // where we stashed this in the HEADER_SIZE bytes directly before the
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
