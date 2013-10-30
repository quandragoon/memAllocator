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
#define SIZE_T_SIZE (sizeof(size_t) * 3 + 4)

struct free_list {
  struct free_list* next;
  size_t size;
};

typedef struct free_list Free_List;
// The smallest aligned value that will hold the header for the free list.
#define FREE_LIST_SIZE (ALIGN(sizeof(Free_List)))

Free_List* freeListBins[SIZE_T_SIZE];

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
    Free_List *this = freeListBins[i];
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
  for (int i = 0; i < SIZE_T_SIZE; i++) {
    freeListBins[i] = NULL;
  }
  return 0;
}

// Returns the upper bound of the log in O(lg N) for N-bit num.
// Bit hack attained from Bit Twiddling Hacks page.
size_t upper_bound_log(size_t val) {
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
size_t log_upper(size_t val) {
  if (val == 0)
    return 0;
  size_t next = val - 1;
  size_t r = 0;
  // since we have subtracted 1, the number of bits
  // upto most significant 1.
  // val = 10000 -> next = 1111 -> r = 4
  // val = 11010 -> next = 11001 -> r = 5
  while (next) {
    r++;
    next >>= 1;
  }
  return r;
}
//  malloc - Allocate a block by incrementing the brk pointer.
//  Always allocate a block whose size is a multiple of the alignment.
void * my_malloc(size_t size) {
  assert (my_check() != -1);
  // Find the upper bound lg of size to find corresponding bin
  // If bin is not null, we allocate
  size += FREE_LIST_SIZE;
  size_t aligned_size = ALIGN(size);
  size_t lg_size = upper_bound_log(aligned_size);
  assert (log_upper(aligned_size) == upper_bound_log(aligned_size));
  Free_List *cur = freeListBins[lg_size];
   
  Free_List *prev = NULL;
  while (cur != NULL && cur->size < aligned_size) {
    // Iterate through list until we find accurate size.
    prev = cur;
    cur = cur->next;
  }
  if (cur != NULL) {
    // Extract the node if we found it.
    if (prev == NULL) {
      freeListBins[lg_size] = cur->next;
    }
    else {
      prev->next = cur->next;
    }
    return (void *)((char *) cur + FREE_LIST_SIZE);
  }
  size_t index = lg_size + 1;
  while (index < SIZE_T_SIZE) {
    // Find big enough de-allocated block
    if (freeListBins[index] != NULL) {
      cur = freeListBins[index]; 
      if (cur->size - aligned_size > FREE_LIST_SIZE) {
        Free_List* chunk = (Free_List *)((char *) cur + aligned_size);
        // Since we allocate aligned sizes, the leftover chunk is also
        // going to be aligned.
        chunk->size = cur->size - aligned_size;
        cur->size = aligned_size;
        chunk->next = NULL;
        void* ptr = (void *)((char *) chunk + FREE_LIST_SIZE);
        my_free(ptr);
      }
      freeListBins[index] = cur->next;
      //NOTE: We need a break up procedure to be more efficient
      //since this will be WAY more memory than is needed.
      return (void *)((char *) cur + FREE_LIST_SIZE);
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
    // FREE_LIST_SIZE bytes.
    ((Free_List*)p)->next = NULL;
    ((Free_List*)p)->size = aligned_size;

    // Then, we return a pointer to the rest of the block of memory,
    // which is at least size bytes long.  We have to cast to uint8_t
    // before we try any pointer arithmetic because voids have no size
    // and so the compiler doesn't know how far to move the pointer.
    // Since a uint8_t is always one byte, adding FREE_LIST_SIZE after
    // casting advances the pointer by FREE_LIST_SIZE bytes.
    return (void *)((char *)p + FREE_LIST_SIZE);
  }
}

// free - Freeing a block does nothing.
void my_free(void *ptr) {
  Free_List* cur = (Free_List*)((char *)ptr - FREE_LIST_SIZE); 
  size_t size = cur->size;
  // int s = (int) size;
  // printf("Size of Freed: %d \n", s);

  // Want to place freed block in a bin
  // that will allow it to be sufficient
  // for any future query of that size (2^k).
  // In other words, put in kth bin if
  // 2^(k-1) < size <= 2^k
  size_t index = upper_bound_log(size);
  // Append to front of free list
  cur->next = freeListBins[index];
  freeListBins[index] = cur;
}

// realloc - Implemented simply in terms of malloc and free
void * my_realloc(void *ptr, size_t size) {
  void *newptr;
  size_t copy_size;
  
  size_t aligned_size = ALIGN(size + FREE_LIST_SIZE);
  // Get the size of the old block of memory.  Take a peek at my_malloc(),
  // where we stashed this in the FREE_LIST_SIZE bytes directly before the
  // address we returned.  Now we can back up by that many bytes and read
  // the size by pulling it from the free list header.
  Free_List* mem = (Free_List*)((uint8_t*)ptr - FREE_LIST_SIZE);
  copy_size = mem->size;
  // If the new block is smaller than the old one, we don't need to copy.
  // Just return original pointer and shave off the end.
  if (aligned_size < copy_size) {
    Free_List* extra = (Free_List*)((uint8_t*)mem + aligned_size);
    extra->size = copy_size - aligned_size;
    extra->next = NULL;
    my_free(extra);
    mem->size = aligned_size;
    return ptr;
  }

  // Allocate a new chunk of memory, and fail if that allocation fails.
  newptr = my_malloc(size);
  if (NULL == newptr)
    return NULL;

  // This is a standard library call that performs a simple memory copy.
  memcpy(newptr, ptr, copy_size - FREE_LIST_SIZE);

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
