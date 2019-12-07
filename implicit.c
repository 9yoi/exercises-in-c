#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "allocator.h"
#include "debug_break.h"

#define HEADER_SIZE 8
/* Get pointer to start of header given pointer to payload */
#define GET_HEADER(bp) ((char *)(bp) - HEADER_SIZE)
/* Get size of block given pointer to header*/
#define GET_SIZE(p) (*(unsigned long *)(p) & ~0x7)
/* Gets status of free or allocated in bits given pointer to header*/
#define GET_ALLOC(p) (*(unsigned long *)(p) & 0x1)
/* Checks if block is free or allocated given pointer to header*/
#define IS_FREE(p) (GET_ALLOC(p) == 0)
/* Sets allocated bit in last bit of size */
#define SET_ALLOC(size, alloc) ((size) | (alloc))
/* Updates pointer with new value */
#define ASSIGN(p, val) (*(unsigned long *)(p) = (val))
/* Move block pointer that points to payload to next block*/
#define NEXT_BLOCK(bp) ((char *)(bp) + GET_SIZE(GET_HEADER(bp)))

static void *segment_start;
static void *segment_end;
static size_t segment_size;
static size_t size_used;

enum header_type {FREE_HEADER, SET_HEADER};

/* Sets size and last bit of block to 1 to allocate and 0 if free.*/
void set_header_block(void *segment, int header_type, unsigned long size){
    // if allocating, save the remaining size first
    unsigned long size_left;
    if (header_type == 1) {
        size_left = GET_SIZE(GET_HEADER(segment)) - size;
    }
    ASSIGN(GET_HEADER(segment), SET_ALLOC(size, header_type));
    if (header_type == 1 && size_left > 0){
      ASSIGN(GET_HEADER(NEXT_BLOCK(segment)), SET_ALLOC(size_left, FREE_HEADER));
    }
}

bool myinit(void *heap_start, size_t heap_size) {
    /* This must be called by a client before making any allocation
     * requests.  The function returns true if initialization was 
     * successful, or false otherwise. The myinit function can be 
     * called to reset the heap to an empty state. When running 
     * against a set of of test scripts, our test harness calls 
     * myinit before starting each new script.
     */
    segment_start = heap_start;
    segment_size = heap_size;
    segment_end = (char *)segment_start + segment_size;
    size_used = 0;
    set_header_block((char *)segment_start + HEADER_SIZE, FREE_HEADER, segment_size);
    return true;
}

/* Function: roundup
 * -----------------
 * This function rounds up the given number to the given multiple, which
 * must be a power of 2, and returns the result.  (you saw this code in lab1!).
 */
size_t roundup(size_t sz, size_t mult) {
    return (sz + mult-1) & ~(mult-1);
}

void *find_next_block(size_t size_needed){
    // approach of bump allocator
    // return (char *)segment_start + size_used + HEADER_SIZE;
    
    char *base_ptr = (char *)segment_start + HEADER_SIZE;
    while (base_ptr + size_needed <= (char *)segment_end) {
      void *header_ptr = GET_HEADER(base_ptr);
      if (IS_FREE(header_ptr) && (GET_SIZE(header_ptr) >= size_needed)) {
        return base_ptr;
      }
      base_ptr = NEXT_BLOCK(base_ptr);
    }
    return NULL;
}

void *mymalloc(size_t requested_size) {
    if (requested_size > MAX_REQUEST_SIZE) {
        return NULL;
    }
    void *ptr;
    size_t size_needed = roundup(requested_size, ALIGNMENT) + HEADER_SIZE;
    if (size_needed + size_used > segment_size) {
        return NULL;
    }
    if ((ptr = find_next_block(size_needed)) != NULL) {
        set_header_block(ptr, SET_HEADER, size_needed);
        size_used += size_needed;
        return ptr;
    };
    // No suitable space found.
    return NULL;
}

void myfree(void *ptr) {
    set_header_block(ptr, FREE_HEADER, GET_SIZE(GET_HEADER(ptr))); 
}

void *myrealloc(void *old_ptr, size_t new_size) {
    void *new_ptr = mymalloc(new_size);
    memcpy(new_ptr, old_ptr, new_size);
    myfree(old_ptr);
    return new_ptr;
}

void dump_heap() {
    printf("Heap segment starts at address %p, ends at %p. %lu bytes currently used.", 
        segment_start, (char *)segment_start + segment_size, size_used);
    for (int i = 0; i < size_used; i++) {
        unsigned char *cur = (unsigned char *)segment_start + i;
        if (i % 32 == 0) {
            printf("\n%p: ", cur);
        }
        printf("%02x ", *cur);
    }
}

bool validate_heap() {
    /* TODO: remove the line below and implement this to 
     * check your internal structures!
     * Return true if all is ok, or false otherwise.
     * This function is called periodically by the test
     * harness to check the state of the heap allocator.
     * You can also use the breakpoint() function to stop
     * in the debugger - e.g. if (something_is_wrong) breakpoint();
     */
   if (size_used > segment_size) {
        printf("Oops! Have used more heap than total available?!\n");
        dump_heap();
        breakpoint();   // call this function to stop in gdb to poke around
        return false;
    }
   //  dump_heap();
    return true;
}


