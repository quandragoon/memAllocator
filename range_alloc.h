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
    #define MIN_SIZE 32
  #elif (TRACE_CLASS == 1)
    #define MIN_SIZE 4
  #elif (TRACE_CLASS == 2)
    #define MIN_SIZE 128
  #elif (TRACE_CLASS == 3)
    #define MIN_SIZE 8
  #elif (TRACE_CLASS == 4)
    #define MIN_SIZE 128
  #elif (TRACE_CLASS == 5)
    #define MIN_SIZE 1
  #elif (TRACE_CLASS == 6)
    #define MIN_SIZE 128
  #elif (TRACE_CLASS == 7)
    #define MIN_SIZE 1024
  #elif (TRACE_CLASS == 8)
    #define MIN_SIZE 16
  #else
    #define MIN_SIZE 64
  #endif
#endif


#ifndef MIN_DIFF
  #if (TRACE_CLASS == 0)
    #define MIN_DIFF 16
  #elif (TRACE_CLASS == 1)
    #define MIN_DIFF 8
  #elif (TRACE_CLASS == 2)
    #define MIN_DIFF 16
  #elif (TRACE_CLASS == 3)
    #define MIN_DIFF 256
  #elif (TRACE_CLASS == 4)
    #define MIN_DIFF 1024
  #elif (TRACE_CLASS == 5)
    #define MIN_DIFF 128
  #elif (TRACE_CLASS == 6)
    #define MIN_DIFF 1024
  #elif (TRACE_CLASS == 7)
    #define MIN_DIFF 128
  #elif (TRACE_CLASS == 8)
    #define MIN_DIFF 4
  #else
    #define MIN_DIFF 128
  #endif
#endif

#ifndef MAX_DIFF
  #if (TRACE_CLASS == 0)
    #define MAX_DIFF 262144
  #elif (TRACE_CLASS == 1)
    #define MAX_DIFF 131072
  #elif (TRACE_CLASS == 2)
    #define MAX_DIFF 128
  #elif (TRACE_CLASS == 3)
    #define MAX_DIFF 32768
  #elif (TRACE_CLASS == 4)
    #define MAX_DIFF 262144
  #elif (TRACE_CLASS == 5)
    #define MAX_DIFF 262144
  #elif (TRACE_CLASS == 6)
    #define MAX_DIFF 262144
  #elif (TRACE_CLASS == 7)
    #define MAX_DIFF 262144
  #elif (TRACE_CLASS == 8)
    #define MAX_DIFF 131072
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
#define NUM_BINS 26

struct free_list {
  struct free_list* next;
  struct free_list* prev;
  size_t size;
  short free;
};

typedef struct free_list Header;

struct footer {
  size_t size;
};
typedef struct footer Footer;
// The smallest aligned value that will hold the header for the free list.
#define HEADER_SIZE (ALIGN(sizeof(Header)))
#define FOOTER_SIZE (ALIGN(sizeof(Footer)))
#define TOTAL_EXTRA_SIZE HEADER_SIZE + FOOTER_SIZE

Header* FreeList[NUM_BINS];
void* heap_lo;

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
    Header *cur = (Header*)p;
    size = ALIGN(cur->size);
    p += size;
  }

  if (p != hi) {
    printf("Bad headers did not end at heap_hi!\n");
    printf("heap_lo: %p, heap_hi: %p, size: %lu, p: %p\n", lo, hi, size, p);
    return -1;
  }

  for (int i=0; i < NUM_BINS; i++) {
    Header *this = FreeList[i];
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
  for(int i = 0; i < NUM_BINS; i++)
    FreeList[i] = NULL;
  heap_lo = my_heap_lo();
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
static inline void split (Header* cur, size_t size, size_t index)
{
  Header* ptr = NULL;
  size_t s = 1 << index;
  while (index > 0 && size != index){
    index--;
    s >>= 1;
    ptr = (Header*)((char*)cur + s);
    ptr->size = s;
    ptr->next = FreeList[index];
    FreeList[index] = ptr;
  }
  cur->size = size;
  return;
}

static inline void add_to_list(Header* cur) {
    size_t index = log_upper(cur->size);
    Header* p = NULL;
    Header* c = FreeList[index];
    while(c && c->size < cur->size){
      p = c;
      c = c->next;
    }
    cur->next = c;
    if(c)
      c->prev = cur;
    if (!p){
      FreeList[index] = cur;
      cur->prev = NULL;
    }
    else{
      p->next = cur;
      cur->prev = p;
    }
    cur->free = 1;
}

// When binning by ranges with variable size allocated blocks, 
// shaves off extra portion of chunk and frees it
// prior to allocating.
// This is to allocate exactly what the user asked for and conserve
// the extra in the free list bins.

static inline void chunk (Header* cur, size_t aligned_size){
    Header* chunk = (Header*)((char*)cur + aligned_size);
    chunk->size = cur->size - aligned_size;
    cur->size = aligned_size;
    Footer* chunk_f = (Footer*)((char*)chunk + chunk->size - FOOTER_SIZE);
    chunk_f->size = chunk->size;
    Footer* cur_f = (Footer*)((char*)cur + aligned_size - FOOTER_SIZE);
    cur_f->size = aligned_size;
    add_to_list(chunk);
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
  assert (my_check() == 0);
  // Find the upper bound lg of size to find corresponding bin
  // If bin is not null, we allocate

  size += TOTAL_EXTRA_SIZE;
  size_t aligned_size = max(ALIGN(size), MIN_SIZE);
  size_t lg_size = log_upper(aligned_size);
  size_t index = lg_size + 1;

  Header *prev, *cur;
  prev = NULL;
  cur = FreeList[lg_size];

  while (cur && cur->size < aligned_size){
    prev = cur;
    cur = cur->next;
  }
  if (cur){
    if(!prev){
      FreeList[lg_size] = cur->next;
      if (cur->next)
        cur->next->prev = NULL;
    }
    else {
      prev->next = cur->next;
      if (cur->next)
        cur->next->prev = prev;
    }
    cur->prev = NULL;
    cur->next = NULL;
    cur->free = 0;
    return (void*)((char*)cur + HEADER_SIZE);
  }

  while (index < NUM_BINS){
    if (FreeList[index]){
      Header* c = FreeList[index];
      /*
      if (c->size - aligned_size > MAX_DIFF)
        break;
        */

      FreeList[index] = c->next;
      if (c->next)
        c->next->prev = NULL;
      c->prev = NULL;
      c->next = NULL;
      c->free = 0;
      
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
    ((Header*)p)->prev = NULL;
    ((Header*)p)->free = 0;
    ((Header*)p)->size = aligned_size;
    ((Footer*)((char*)p + aligned_size - FOOTER_SIZE))->size = aligned_size;
    // Then, we return a pointer to the rest of the block of memory,
    // which is at least size bytes long.  We have to cast to uint8_t
    // before we try any pointer arithmetic because voids have no size
    // and so the compiler doesn't know how far to move the pointer.
    // Since a uint8_t is always one byte, adding HEADER_SIZE after
    // casting advances the pointer by HEADER_SIZE bytes.
    return (void *)((char *)p + HEADER_SIZE);
  }
}

// Removes a node from a list.
void remove_from_list(Header* node){
  size_t ind = log_upper(node->size);
  if (!node->prev){
    FreeList[ind] = node->next;
    if (node->next)
      node->next->prev = NULL;
  }
  else{
    node->prev->next = node->next;
    if (node->next)
      node->next->prev = node->prev;
  }
  node->prev = NULL;
  node->next = NULL;
}

// takes a mid block, checks left and right to see if coalescing is possible.
Header* coalesce (Header * mid){
  size_t total = mid->size;
  Header* right = (Header*)((char*)mid + mid->size);
  // check right
  if ((void*)right != my_heap_hi()+1){
    if (right->free){
      remove_from_list(right);
      total += right->size;
    }
  }
  //check left
  if ((void*)mid == heap_lo){
    mid->size = total;
    ((Footer*)((char*)mid + mid->size - FOOTER_SIZE))->size = total;
    return mid;
  }
  Footer* left_f = (Footer*)((char*)mid - FOOTER_SIZE);
  Header* left = (Header*)((char*)mid - left_f->size);
  if (!left->free){
    mid->size = total;
    ((Footer*)((char*)mid + mid->size - FOOTER_SIZE))->size = total;
    return mid;
  }
  remove_from_list(left);
  total += left->size;
  mid = left;
  mid->size = total;
  ((Footer*)((char*)mid + mid->size - FOOTER_SIZE))->size = total;
  return mid;
}

// free - Find the appropriate bin for a freed
// element by accessing its header and finding
// its size.
// Places size in bin k such that 2^(k-1) < size <= 2^k.
void my_free(void *ptr) {
  Header* cur = (Header*)((char*)ptr - HEADER_SIZE);
  cur = coalesce(cur);
  size_t index = log_upper(cur->size);

  Header* p = NULL;
  Header* c = FreeList[index];

  while(c && c->size < cur->size){
    p = c;
    c = c->next;
  }

  cur->next = c;
  if(c)
    c->prev = cur;

  if (!p){
    FreeList[index] = cur;
    cur->prev = NULL;
  }
  else{
    p->next = cur;
    cur->prev = p;
  }
  // Set node as free.
  cur->free = 1;
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


  size_t aligned_size = ALIGN(size + HEADER_SIZE);
  // Here is the header we are working with.
  Header* mem = (Header*)((char*)ptr - HEADER_SIZE);
  copy_size = min(mem->size, aligned_size) - HEADER_SIZE;


  // If the new block is smaller than the old one, we have to stop copying
  // early so that we don't write off the end of the new block of memory.
  
  if (aligned_size + MIN_DIFF < copy_size){
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
    Footer* f = (Footer*)((char*)mem + aligned_size - FOOTER_SIZE);
    f->size = aligned_size;
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
