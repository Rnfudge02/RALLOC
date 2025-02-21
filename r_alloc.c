//Header file include
#include "r_alloc.h"

//Macro definitions
#define KB *1024
#define MB *1024*1024
#define ARENA_SIZE 8*1024*1024
#define MIN_ALLOC_SIZE 32       //Minimum allocation check, used to prevent unnessecary small allocations

//MAP ANONYMOUS will not show as defined
#define MAP_ANONYMOUS 0x20

//Standard Library Includes
#include <sys/mman.h>
#include <stdint.h>

//Notes: When referring to saving a pointer, this is in reference to the DLL implementation I was originally using
//meaning the new implemnetation uses one less pointer
//This is a single threaded implementation

//Memory manager structure, should only be one instance at a time
//Needs to hold a global free list, for large alocations, (greater than the 8MB arena size)
//As well as a pointer to the head of the SLL structure for memarena
//The global free list is used so that the manager can reallocate global blocks (>8MB) after they have been freed
struct __memman {
    struct __memarena* arenas;
    struct __memblck* global_free_list;     //
};

//Memory arena, large block of free data acquired via mmap
//SLL > DLL because, O(1) insertion at end
//Saves a pointer
//Traversal is simplified, but searching will likely be slower
//Arena free list used to reallocate memory that has been freed, but isstill help bbecause of mmap by the process
struct __memarena {
    struct __memarena* next_arena;      //Single linked list, saves a pointer and one-way traversal
    void* free_list;                    //Pointer to the first free block
    uint8_t data[];                     //Start of arena memory
};

//Memory block, subdivided from arena
struct __memblck {
    size_t size;                        //Size of allocated block
    struct __memblck* next_block;       //Single linked list, saves a pointer
    bool active;                        //Used for freeing
};

//Global memory manager, to this file at least
static bool manager_initialized = false;
static struct __memman* mem0 = NULL;

//Local functions
size_t __alloc_size(size_t);
static void* __block_to_ptr(struct __memblck*);
static struct __memblck* __create_new_allocation(struct __memman*, size_t);
static struct __memblck* __find_arena_block(struct __memman*, size_t);
static struct __memblck* __find_global_block(struct __memman*, size_t);
static void __free_arena_block(struct __memman*, struct __memblck*);
static void __free_global_block(struct __memman*, struct __memblck*);
static struct __memblck* __ptr_to_block(void*);
static struct __memarena* __find_container_arena(struct __memman*, struct __memblck*);
static void __aggregate_arena_blocks(struct __memarena*, struct __memblck*);
static void __aggregate_global_blocks(struct __memman*, struct __memblck*);
static struct __memblck* __find_previous_block(struct __memarena*, struct __memblck*);
static void __remove_free_list_entry(struct __memarena*, struct __memblck*);
static void __remove_arena(struct __memman*, struct __memarena*);
static struct __memman* get_manager();

//User-facing functions implementation

void* r_malloc(size_t size) {
    //If alloc size is 0, do nothing
    if (size == 0) {
        return NULL;
    }

    //Get access to the memory manager_alloc_s
    struct __memman* mman = get_manager();

    //Align the memory size to OS-bitness for improved efficiency
    size_t alloc_size = __alloc_size(size);
    struct __memblck* newblck = NULL;

    //If the size is less than 1/16th of an arena, use the arena block, if larger, get a global block
    if (alloc_size < ARENA_SIZE / 16) {
        newblck = __find_arena_block(mman, alloc_size);
    }

    else {
        newblck = __find_global_block(mman, alloc_size);
    }

    //If the global/local allocation failed, a new allocation is needed under the respective subtype
    if (!newblck) {
        newblck = __create_new_allocation(mman, alloc_size);
    }

    if (newblck) {
        newblck->active = true;
        return __block_to_ptr(newblck);
    }

    else {
        return NULL;
    }
}

void* r_realloc(void *ptr, size_t size) {
    //If the pointer is null, memory hasn't been created
    if (ptr == NULL) {
        return r_malloc(size);
    }

    //If the size is 0, the user likely wants to free the memory
    if (size == 0) {
        r_free(ptr);
        return NULL;
    }

    //After edge cases have been handled, get the block structure, and retrieve its old size
    struct __memblck* block = __ptr_to_block(ptr);
    size_t old_size = block->size - sizeof(struct __memblck);

    //If the size of the block is greater than whats being requested
    if (old_size >= size) {
        return ptr;
    }

    //Otherwise, call malloc, get a new allocation, if it returns NULL, pass that to the user
    void* new_ptr = r_malloc(size);
    if (new_ptr == NULL) {
        return NULL;
    }

    //Copy the data from the old allocation to the new one
    for (size_t i = 0; i < old_size; i++) {
        ((uint8_t*)new_ptr)[i] = ((uint8_t*)ptr)[i];
    }

    //Free the old pointer and return the old one
    r_free(ptr);
    return new_ptr;
}

void r_free(void *ptr) {
    //If pointer is already NULL, do nothing
    if (ptr == NULL) {
        return;
    }

    struct __memblck* block = __ptr_to_block(ptr);

    struct __memman* mman = get_manager();

    //Call the appropriate freeing function
    if (block->size <= ARENA_SIZE) {
        __free_arena_block(mman, block);
    }

    else {
        __free_global_block(mman, block);
    }
}

size_t r_alloc_size(void *ptr) {
    //If pointer is not valid, there is no size associated with it
    if (ptr == NULL) {
        return 0;
    }

    //Get the block structure, retrieve the size of the block, and remove the size of the metadata, return the result to user
    struct __memblck* block = __ptr_to_block(ptr);
    return block->size - sizeof(struct __memblck);

}

bool r_allocated(void *ptr) {
    //If pointer is NULL its defintely not allocated
    if (ptr == NULL) {
        return false;
    }

    //Get memory block and manager
    struct __memblck* blk = __ptr_to_block(ptr);
    struct __memman* mman = get_manager();
    
    // Check global blocks
    struct __memblck* global = mman->global_free_list;
    while (global) {
        if (global == blk) {
            return global->active;
        }
        global = global->next_block;
    }
    
    // Check arenas
    struct __memarena* arena = mman->arenas;
    while (arena) {
        if ((uint8_t*)blk >= arena->data && (uint8_t*)blk < arena->data + ARENA_SIZE) {
            struct __memblck* current = arena->free_list;

            //Check if the block is found in the arena free lists
            while (current) {
                if (current == blk) {
                    return false;
                }

                //Traverse the list
                current = current->next_block;
            }

            return true;
        }

        //Traverse arenas
        arena = arena->next_arena;
    }
    return false;
}

size_t r_total_allocated(void) {
    //Create a zeroed variable to accumulate the size of all allocations
    size_t total_allocated = 0;
    //Get the start of the Arena SLL
    struct __memman* mem0 = get_manager();
    struct __memarena* arena = mem0->arenas;

    //Get arena allocations
    while (arena) {
        uint8_t* arena_end = arena->data + ARENA_SIZE;
        struct __memblck* blk = (struct __memblck*)arena->data;
        while ((uint8_t*)blk < arena_end) {
            //If block is active, accumulate it
            if (blk->active) {
                total_allocated += blk->size - sizeof(struct __memblck);
            }
            //go to next block
            blk = (struct __memblck*)((uint8_t*)blk + blk->size);
        }
        //Traverse to next arena
        arena = arena->next_arena;
    }

    //Get the head of the global block SLL
    struct __memblck* global_block = mem0->global_free_list;
    //Traverse the list
    while (global_block) {
        //If the global block is active, accumulate its size
        if (global_block->active) {
            total_allocated += global_block->size - sizeof(struct __memblck);
        }

        //Iterate to next block
        global_block = global_block->next_block;
    }

    //After all blocks that are active have been accumulated, return
    return total_allocated;
}

//  Helper function implementations

//Helper function to align size to system's size_t and include metadata
size_t __alloc_size(size_t size) {
    size_t aligned_size = (size + sizeof(size_t) - 1) & ~(sizeof(size_t) - 1);
    return sizeof(struct __memblck) + aligned_size;
}

//Use bidirectional coalescing for the arena memory blocks
static void __aggregate_arena_blocks(struct __memarena* arena, struct __memblck* blk) {
    // Forward coalesce (check next block)
    struct __memblck* next = (struct __memblck*)((char*)blk + blk->size);
    //
    if ((char*)next < arena->data + ARENA_SIZE && !next->active) {
        blk->size += next->size;
        __remove_free_list_entry(arena, next);
    }

    //Backward coalesce (check previous block)
    struct __memblck* prev = __find_previous_block(arena, blk);
    //
    if (prev && !prev->active && (char*)prev + prev->size == (char*)blk) {
        prev->size += blk->size;
        __remove_free_list_entry(arena, blk);
        blk = prev;
    }
}

//Helper function to group large blocks
static void __aggregate_global_blocks(struct __memman* mman, struct __memblck* blk) {

    //Get pointer to start of global free list, need another prev one to have access to two blocks simultanouslty
    struct __memblck* current = mman->global_free_list;
    struct __memblck* prev = NULL;
    
    //Walk through the SLL
    while (current) {
        //Check if blocks are adjacent - forward
        if ((char*)current + current->size == (char*)blk) {
            current->size += blk->size;

            if (prev) {
                prev->next_block = current->next_block;
            }

            else {
                mman->global_free_list = current->next_block;
            }
            
            blk = current;
        }
        
        //Check if blocks are adjacent - previous
        else if ((char*)blk + blk->size == (char*)current) {
            blk->size += current->size;
            if (prev) prev->next_block = current->next_block;
            else mman->global_free_list = current->next_block;
        }
        
        //Iterate to next element of SLL
        prev = current;
        current = current->next_block;
    }
}


//Helper function for getting block back from pointer
//Does the opposite of above, just adds the metadata back to the pointer and returns
//This gets the pointer to the block metadata again
static void* __block_to_ptr(struct __memblck* blk) {
    return (void*)((uint8_t*)blk + sizeof(struct __memblck));
}

//Helper function for creating new allocations
static struct __memblck* __create_new_allocation(struct __memman* mman, size_t alloc_size) {
    //Should this be allocated as a global or local block
    if (alloc_size < ARENA_SIZE / 16) {
        size_t arena_total_size = sizeof(struct __memarena) + ARENA_SIZE; //Header + 8MB, allocate arena

        //Request memory from the kernel via syscall
        struct __memarena* new_arena = mmap(NULL, arena_total_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

        //If getting memory from the kernel failed
        if (new_arena == MAP_FAILED) {
            return NULL;
        }

        //Set-up the arena metadata
        new_arena->next_arena = mman->arenas;
        new_arena->free_list = (struct __memblck*)(new_arena->data);

        //Create the initial free block - Size of whole arena, it'll be split later
        struct __memblck* initial_block = (struct __memblck*)(new_arena->data);
        initial_block->size = ARENA_SIZE;
        initial_block->next_block = NULL;

        //Update head of arena SLL
        mman->arenas = new_arena;

        //return pointer to the (relatively) massive memory block, now being retrieved from find_arena_block again.
        return __find_arena_block(mman, alloc_size);
    }

    //Global allocation
    else {
        //Try to allcate memory for requested data size + header if failed, will retrun null
        size_t total_size = alloc_size + sizeof(struct __memblck);
        void* mem_addr = mmap(NULL, total_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

        if (mem_addr == MAP_FAILED) {
            return NULL;
        }

        //Since allocation passed, initialize block metadata
        struct __memblck* global_block = (struct __memblck*) mem_addr;
        global_block->size = total_size;
        global_block->active = true;

        return global_block;
    }
}

//Helper function to find arena block - For smaller allocations
static struct __memblck* __find_arena_block(struct __memman* mman, size_t alloc_size) {
    //Get start of SLL
    struct __memarena* p_arenas = mman->arenas;

    //While the arenas pointer is valid (not NULL) (iterate through SLL)
    while (p_arenas) {
        //Get prev as address of the previous arenas free list
        
        struct __memblck** prev = &p_arenas->free_list;
        struct __memblck* current = p_arenas->free_list;

        //Traverse the free list of current arena
        while (current) {
            //If the size of the current 
            if (current->size >= alloc_size) {
                //Remove the block from the free list
                *prev = current->next_block;

                //Split the block if sufficient remaining space
                size_t space_remaining = current->size - alloc_size;

                if (space_remaining >= sizeof(struct __memblck) + MIN_ALLOC_SIZE) {
                    struct __memblck* split = (struct __memblck*)((char*)current + alloc_size);
                    split->size = space_remaining;
                    split->next_block = p_arenas->free_list;
                    p_arenas->free_list = split;
                    current->size = alloc_size;
                };

                return current;
            }

            //Make the current block the previous block, and the next block the current block
            prev = &current->next_block;
            current = current->next_block;
        }

        //Iterate to next arena, while loop guards will terminate when pointer becomes null
        p_arenas = p_arenas->next_arena;
    }

    //No suitable blocks were available
    return NULL;
}

//Helper function that returns the arena associated with a memory block
static struct __memarena* __find_container_arena(struct __memman* mman, struct __memblck* memblck) {
    //Get pointer to start of arenas array
    struct __memarena* p_arenas = mman->arenas;

    //While the pointer is valid...
    while (p_arenas) {
        char* p_memblck = ((char*)memblck);

        //Check if block is contained within the arenas data space (>= start, < end), needs to be casted to char
        if (p_arenas->data <= p_memblck && p_arenas->data + ARENA_SIZE > p_memblck) {
            return p_arenas;
        }

        //Iterate through arenas  array
        p_arenas = p_arenas->next_arena;
    }

    //If nothing is found, return NULL
    return NULL;
}

//Helper function to find global block
static struct __memblck* __find_global_block(struct __memman* mman, size_t alloc_size) {
    struct __memblck** prev = &mman->global_free_list;
    struct __memblck* current = mman->global_free_list;

    //While traversing global blocks
    while (current) {
        if (current->size >= alloc_size) {
            *prev = current->next_block;

            size_t space_remaining = current->size - alloc_size;

            //Splitting logic
            if (space_remaining >= sizeof(struct __memblck) + MIN_ALLOC_SIZE) {
                struct __memblck* split = (struct __memblck*)((char*)current + alloc_size);
                split->size = space_remaining;
                split->next_block = mman->global_free_list;
                mman->global_free_list = split;
                current->size = alloc_size;
            }

            return current;
        }

        //Iterate to next block
        prev = &current->next_block;
        current = current->next_block;

    }

    //If no suitable block is found
    return NULL;
}

//Helper function to find the previous block
static struct __memblck* __find_previous_block(struct __memarena* arena, struct __memblck* blk) {
    //Starting from the head of the free list
    struct __memblck* blk_head = arena->free_list;

    //While the list is able to be traversed (blk_head != NULL)
    while (blk_head) {
        //If the pointer to the next block is equal to the argument pointer block, i've found it
        if (blk_head->next_block == blk) {
            return blk_head;
        }
        //Else continue traversal
        blk_head = blk_head->next_block;
    }

    return NULL;
}

//Helper function to free an arenas block
static void __free_arena_block(struct __memman* mman, struct __memblck* blk) {
    //Find containing arena
    struct __memarena* arena = __find_container_arena(mman, blk);
    if (!arena) {
        //FATAL ERROR

        return;
    }

    //Aggregate with adjacent free blocks
    __aggregate_arena_blocks(arena, blk);

    //Add to arena's free list
    blk->next_block = arena->free_list;
    arena->free_list = blk;
    blk->active = false;

    // Check if entire arena is free, if so, free it with the kernel
    //Need the cast here, if not for code to compile, at least to remove errors
    struct __memblck* mb = (struct __memblck*) arena->free_list;

    if (mb->size == ARENA_SIZE) {
        __remove_arena(mman, arena);
    }
}

//Helper function to free a global block
static void __free_global_block(struct __memman* mman, struct __memblck* blk) {
    //Aggregate with adjacent blocks in global free list
    __aggregate_global_blocks(mman, blk);

    // Add to global free list - Its already been allocated
    blk->next_block = mman->global_free_list;
    mman->global_free_list = blk;
    blk->active = false;
}

//Helper for getting memory block from pointer
//Cast to pointer of memory block, get unsigned integer expression of ptr and remove the size of the header
//To retrieve starting address of the memory block
static struct  __memblck* __ptr_to_block(void* ptr) {
    return (struct __memblck*)((uint8_t*) ptr - sizeof(struct __memblck));
}

//Helper for removing block from arena or global free list
void __remove_free_list_entry(struct __memarena* arena, struct __memblck* memblck) {
    struct __memblck** prev = &arena->free_list;
    struct __memblck* current = *prev;

    //Iterate through free list while current pointer is not NULL
    while(current) {
        if (current == memblck) {
            *prev = current->next_block;
            return;
        }
        prev = &current->next_block;
        current = current->next_block;
    }
}

//Helper function for gaining access to the memory manager
static struct __memman* get_manager() {
    //Create the manager if it doesnt exist, and initialize it's pointers to NULL
    if (!manager_initialized) {
        mem0 = mmap(NULL, sizeof(struct __memman), PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        mem0->arenas = NULL;
        mem0->global_free_list = NULL;
        manager_initialized = true;
    }

    //Return pointer to the memory manager
    return mem0;
}

//Helper function for removing arena from memory
void __remove_arena(struct __memman* mman, struct __memarena* arena) {
    //Manage SLL nodes

    //If arena is the head of the SLL, change head to next
    if (mman->arenas == arena) {
        mman->arenas = arena->next_arena;
    }

    //
    else {
        //Get a copy of the head pointer to modify
        struct __memarena* prev = mman->arenas;
        //Traverse the arenas and find the appropriate one to remove
        while (prev->next_arena !=  arena) {
            //While not found, continue traversal
            prev = prev->next_arena;

        }

        //Unlink the arena to be unmapped
        prev->next_arena = arena->next_arena;

    }

    //Free memory of the arena, and free the size of the metadata + data.
    munmap(arena, sizeof(struct __memarena) + ARENA_SIZE);
}

