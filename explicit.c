#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "allocator.h"
#include "debug_break.h"

#define HEADER_SIZE 8
/* Get pointer to start of header given pointer to payload */
#define GET_HEADER(bp) ((unsigned char *)(bp) - HEADER_SIZE)
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
#define NEXT_BLOCK(bp) ((unsigned char *)(bp) + GET_SIZE(GET_HEADER(bp)))
/* Define minimum payload as 24 for header and struct to prev and next nodes*/
#define MIN_PAYLOAD_SIZE 24
static void *segment_start;
static void *segment_end;
static size_t segment_size;
static size_t size_used;

typedef struct node {
    struct node *prev;
    struct node *next;
} Node;

static Node *head_of_free_list;

Node *cast_to_node(void *ptr) {
    return (Node *)ptr;
}

Node *get_prev_free(Node *node) {
    return node->prev;
}

Node *get_next_free(Node *node) {
   return node->next;
}

void set_prev_free(Node *ptr, Node *prev_block) {
   ptr->prev = prev_block;
}

void set_next_free(Node *ptr, Node *next_block) {
    ptr->next = next_block;
}

/* Find first free block that also fits the size requirement*/
Node *find_free_block(size_t size_required){
    Node *node = head_of_free_list;
    while(node) {
       size_t block_size = GET_SIZE(GET_HEADER(node));
       if (block_size >= size_required){
           return node;
       }
       node = get_next_free(node);
    }
    // none found
    return NULL;
}

Node *find_match_in_free_list(void *segment){
    Node *node = head_of_free_list;
    while(node) {
        if ((unsigned long*)node == (unsigned long*)segment){
            return node;
        }
        node = get_next_free(node);
    }
    return NULL;
}

void remove_free_block(void *segment){
    Node *node = find_match_in_free_list(segment);
    if (!node) {
        return;
    }
    if (node == head_of_free_list){
        head_of_free_list = get_next_free(node);
        return;
    }
    set_next_free(get_prev_free(node), get_next_free(node));
    if (get_next_free(node)) {
        set_prev_free(get_next_free(node), get_prev_free(node));
    }
}

/* Iterate from first free node until node just before segment in memory location*/
Node *find_prev_in_free_list(void *segment){
    Node *node = head_of_free_list;
    while(node) {
        // compare memory location
          if ((!get_next_free(node) &&
           ((unsigned long *)node < (unsigned long *)segment))
          || ((unsigned long *)get_next_free(node) > (unsigned long *)segment)){
         return node;
        }
    node = node->next;
   }
    // None found
    return NULL;
}

/* Iterate find next node, optionally given prev*/
Node *find_next_in_free_list(Node *prev, void *segment){
    if (head_of_free_list &&
        (unsigned long*)head_of_free_list > (unsigned long *)segment) {
        return head_of_free_list;
    }
    if (prev) {
        return get_next_free(prev);
    } else {
        Node *node = head_of_free_list;
        while (node) {
            if ((unsigned long *)node > (unsigned long *)segment) {
                return node;
            }
            node = node->next;
        }
    }
    return NULL;
}


/*insert block after target*/
void insert_in_list(Node *prev, Node *next, void *segment) {
    if(next){
        set_prev_free(next, segment);
    }
    if(prev){
      set_next_free(prev, segment);
    }
}

void update_head(Node *node){
    set_next_free(node, head_of_free_list);
    set_prev_free(head_of_free_list, node);
    head_of_free_list = node;
}

void update_free_list(Node *prev, Node *next, void *segment){
    // If no head, set it to self
    if (!head_of_free_list) {
        head_of_free_list = (Node *)segment;
    }
    else {
    // If there is a free node, find your position in the list and update list.
        // if the free node is the head, update head
        if (next == head_of_free_list) {
            update_head(cast_to_node(segment));
        }  else {
            insert_in_list(prev, next, cast_to_node(segment));
        }
    }
}

bool next_block_in_memory_free(void *segment, Node *next_node){
    if ((unsigned long*)next_node == (unsigned long*)NEXT_BLOCK(segment)) {
        return true;
    } else {
        return false;
    };
}

void mycoalesce(void *segment, Node *next_node) {
   // next block in memory is free
    if(next_block_in_memory_free(segment, next_node)){
     //update size to merged size.
     size_t merged_size = GET_SIZE(GET_HEADER(segment)) +
     GET_SIZE(GET_HEADER(next_node));
     ASSIGN(GET_HEADER(segment), SET_ALLOC(merged_size, 0));
        
    // update linked list of free nodes
      Node *curr_node = (Node *)segment;
      Node *next_next_node = get_next_free(next_node);
      if (next_next_node){
          set_next_free(curr_node, next_next_node);
          set_prev_free(next_next_node, curr_node);
      } else {
          set_next_free(curr_node, NULL);
      }
      // update head if it is one of the nodes getting merged
      if (head_of_free_list == next_node) {
          set_prev_free(segment, NULL);
          head_of_free_list = segment;
      }
  }
}

/* Set payload with pointers to prev and next free block*/
void *assign_free_block(void *block, unsigned long size){

    Node *prev_node = find_prev_in_free_list(block);
    Node *next_node = find_next_in_free_list(prev_node, block);
    
    // Create node with prev and next within payload
    set_prev_free(cast_to_node(block), prev_node);    
    set_next_free(cast_to_node(block), next_node);

    // add free node in the right position in free list
    update_free_list(prev_node, next_node, block);

    // merge one to the right
    if (next_node){
        mycoalesce(block, next_node);
    }

    return block;
}

/* allocate block including header size */
void allocate_segment(void *segment, unsigned long size) {
    
     // if allocating, save the remaining size first
    unsigned long size_left = GET_SIZE(GET_HEADER(segment)) - size;
    
    // Set header
    ASSIGN(GET_HEADER(segment), SET_ALLOC(size, 1));

    // remove node from free list
    remove_free_block(segment);

    // free remainder of block
    if ((size_left > sizeof(Node) + HEADER_SIZE) && NEXT_BLOCK(segment)){
        ASSIGN(GET_HEADER(NEXT_BLOCK(segment)), SET_ALLOC(size_left, 0));
        assign_free_block(NEXT_BLOCK(segment), size_left);
    } else {
        // include padding that couldn't be freed in block size
       ASSIGN(GET_HEADER(segment), SET_ALLOC(size + size_left, 1));     
    }
}

void free_segment(void *segment, unsigned long size) {
    // Set header
    ASSIGN(GET_HEADER(segment), SET_ALLOC(size, 0));
    // assign payload
    assign_free_block(segment, size);
}

bool myinit(void *heap_start, size_t heap_size) {
    segment_start = heap_start;
    segment_size = heap_size;
    segment_end = (char *)segment_start + segment_size;
    size_used = 0;
    head_of_free_list = NULL;
    free_segment((char *)segment_start + HEADER_SIZE, segment_size);
    return true;
}

/* Function: roundup
 * -----------------
n
 * This function rounds up the given number to the given multiple, which
 * must be a power of 2, and returns the result.  (you saw this code in lab1!).
 */
size_t roundup(size_t sz, size_t mult) {
    return (sz + mult-1) & ~(mult-1);
}

void *mymalloc(size_t requested_size) {
    if (requested_size > MAX_REQUEST_SIZE) {
        return NULL;
    }
    void *ptr;
    size_t size_needed = roundup(requested_size, ALIGNMENT) + HEADER_SIZE;
    // need space for header and prev + next payload when freed
    if (size_needed < MIN_PAYLOAD_SIZE) {
        size_needed = MIN_PAYLOAD_SIZE;
    }
    if (size_needed + size_used > segment_size) {
        return NULL;
    }
    if ((ptr = find_free_block(size_needed)) != NULL) {
        allocate_segment(ptr, size_needed);
        size_used += size_needed;
        return ptr;
    };
    // No suitable space found.
    return NULL;
}

void myfree(void *ptr) {
    ASSIGN(GET_HEADER(ptr), SET_ALLOC(GET_SIZE(GET_HEADER(ptr)), 0));
    // TODO: release size
    //size_used -= GET_SIZE(GET_HEADER(ptr));
    assign_free_block(ptr, GET_SIZE(GET_HEADER(ptr)));
    
}

int realloc_in_place(void *old_ptr, size_t new_size) {
    void *next_block = NEXT_BLOCK(old_ptr);
    size_t size_avail = GET_SIZE(GET_HEADER(old_ptr));
    int blocks = 0;
    while (next_block < segment_end && IS_FREE(GET_HEADER(next_block))){
        size_avail += GET_SIZE(GET_HEADER(next_block));
        blocks ++;
        if (GET_SIZE(GET_HEADER(next_block)) <= 0) {
            break;
        }
        next_block = NEXT_BLOCK(next_block);
    }
    if (size_avail >= new_size) {
        return blocks;
    }
    return 0;
};

void *myrealloc(void *old_ptr, size_t new_size) {
 
    // size_t org_size = GET_SIZE(GET_HEADER(old_ptr));
    // size_t org_payload_size = org_size - HEADER_SIZE;
    if (new_size > MAX_REQUEST_SIZE) {
        return NULL;
    }
    size_t size_needed = roundup(new_size, ALIGNMENT) + HEADER_SIZE;
    // need space for header and prev + next payload when freed
    if (size_needed < MIN_PAYLOAD_SIZE) {
        size_needed = MIN_PAYLOAD_SIZE;
    }
    if (size_needed + size_used > segment_size) {
        return NULL;
    }
    // printf("\n MYREALLOC %p, %d", old_ptr, size_needed);
    // if (size_needed <= org_size){
    //  allocate_segment(old_ptr, size_needed);
    //  return old_ptr;
    // }
    // preserve data
    // int blocks;
    // void *org_payload = mymalloc(org_payload_size);
    // memcpy(org_payload, old_ptr, org_payload_size);
    //if ((blocks = realloc_in_place(old_ptr, size_needed)) > 0) {
    //  void *curr_ptr = old_ptr;
        // merge free blocks
    //  while (blocks > 0) {
    //     mycoalesce(curr_ptr, (Node *)NEXT_BLOCK(curr_ptr));
    //     curr_ptr = NEXT_BLOCK(curr_ptr);
    //      blocks --;
    //      printf("%s", "\nlist after COALESCE");
    //      walk_free_list();
    //      walk_heap();
    //   }
    //    allocate_segment(old_ptr, size_needed);
    //    size_used += size_needed;
    //   memset(old_ptr, 0, size_needed);
    //   memcpy(old_ptr, org_payload, org_size);
    //   myfree(org_payload);
    //     return old_ptr;
    // }
    // else {
    // void *new_ptr = mymalloc(new_size);
    //   if (new_ptr) {
            // copy payload but not header
    //       memcpy(new_ptr, old_ptr, roundup(new_size, ALIGNMENT));
    //       myfree(old_ptr);
    //       myfree(org_payload);
    //    }
    //    return new_ptr;
    // }
  void *new_ptr = mymalloc(new_size);
        if (new_ptr) {
            // copy payload but not header
            memcpy(new_ptr, old_ptr, roundup(new_size, ALIGNMENT));
            myfree(old_ptr);
        }
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

void walk_free_list(){
    Node *node = head_of_free_list;
    int count = 0;
    printf("%s", "\nWALKING NODES IN FREE LIST: \n");
    while(node){
        printf("location%p , size %lu, prev %p, next %p\n", node,
            GET_SIZE(GET_HEADER(node)), get_prev_free(node),
             get_next_free(node));
       count ++;
       printf("There are %d free block(s) \n", count);
       node = get_next_free(node);
     }
}

void walk_heap(){
    unsigned char *ptr = (unsigned char *)segment_start + HEADER_SIZE;
    while ((unsigned char *)ptr < (unsigned char *)segment_end) {
        printf("WALKING HEAP\n: %p alloc: %d size: %lu\n", ptr, IS_FREE(GET_HEADER(ptr)),
               GET_SIZE(GET_HEADER(ptr)));
        if (NEXT_BLOCK(ptr) == ptr) {
            break;
        }
        ptr = NEXT_BLOCK(ptr);
        
    }
    
    /// unsigned char *ptr = (unsigned char *)segment_start + HEADER_SIZE;
    //while (ptr < (unsigned char *) segment_end) {
    // size_t size = GET_SIZE(GET_HEADER(ptr));
    // if(!size){
    //   return false;
    //  }
    //   int isFree = GET_ALLOC(GET_HEADER(ptr));
        //check that payload is populated if free
    //  if (isFree){
    //       get_next_free((Node *)ptr);
    //       get_prev_free((Node *)ptr);
    //   }
    //    ptr = NEXT_BLOCK(ptr);
    //  }
    //return true;
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
        // dump_heap();
        breakpoint();   // call this function to stop in gdb to poke around
        return false;
    }

    //walk_free_list();
    //walk_heap();

    return true;
}
 
