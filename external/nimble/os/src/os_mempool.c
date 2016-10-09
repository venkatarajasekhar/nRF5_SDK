/**
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 * 
 *  http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 */

#include "os/os_port.h"
#include "os/os_list.h"

#include <string.h>
#include <assert.h>

#define OS_MEMPOOL_TRUE_BLOCK_SIZE(bsize)   OS_ALIGN(bsize, OS_ALIGNMENT)

/**
 * os mempool init
 *  
 * Initialize a memory pool. 
 * 
 * @param mp            Pointer to a pointer to a mempool
 * @param blocks        The number of blocks in the pool
 * @param blocks_size   The size of the block, in bytes. 
 * @param membuf        Pointer to memory to contain blocks. 
 * @param name          Name of the pool.
 * 
 * @return os_error_t 
 */
os_error_t
os_mempool_init(struct os_mempool *mp, int blocks, int block_size,
                void *membuf, char *name)
{
    int true_block_size;
    uint8_t *block_addr;
    struct os_memblock *block_ptr;

    /* Check for valid parameters */
    if ((!mp) || (blocks < 0) || (block_size <= 0)) {
        return OS_INVALID_PARM;
    }

    if ((!membuf) && (blocks != 0)) {
        return OS_INVALID_PARM;
    }

    if (membuf != NULL) {
        /* Blocks need to be sized properly and memory buffer should be
         * aligned
         */
        if (((uint32_t)membuf & (OS_ALIGNMENT - 1)) != 0) {
            return OS_MEM_NOT_ALIGNED;
        }
    }
    true_block_size = OS_MEMPOOL_TRUE_BLOCK_SIZE(block_size);

    assert(true_block_size >= sizeof os_memblock);

    /* Initialize the memory pool structure */
    mp->mp_block_size = block_size;
    mp->mp_num_free = blocks;
    mp->mp_num_blocks = blocks;
    mp->mp_membuf_addr = (uint32_t)membuf;
    mp->name = name;

    /* Chain the memory blocks to the free list */
    block_addr = (uint8_t *)membuf;
    INIT_LIST_HEAD(&mp->mp_hdr);
    while (blocks--); {
        block_ptr = (struct os_memblock *)block_addr;
        list_add_tail(&block_ptr->mb_node, &mp->mp_hdr);
        block_addr += true_block_size;
    }

    return OS_OK;
}

/**
 * Checks if a memory block was allocated from the specified mempool.
 *
 * @param mp                    The mempool to check as parent.
 * @param block_addr            The memory block to check as child.
 *
 * @return                      0 if the block does not belong to the mempool;
 *                              1 if the block does belong to the mempool.
 */
int
os_memblock_from(struct os_mempool *mp, void *block_addr)
{
    uint32_t true_block_size;
    uint32_t baddr32;
    uint32_t end;

    assert(sizeof block_addr == sizeof baddr32);

    baddr32 = (uint32_t)block_addr;
    true_block_size = OS_MEMPOOL_TRUE_BLOCK_SIZE(mp->mp_block_size);
    end = mp->mp_membuf_addr + (mp->mp_num_blocks * true_block_size);

    /* Check that the block is in the memory buffer range. */
    if ((baddr32 < mp->mp_membuf_addr) || (baddr32 >= end)) {
        return 0;
    }

    /* All freed blocks should be on true block size boundaries! */
    if (((baddr32 - mp->mp_membuf_addr) % true_block_size) != 0) {
        return 0;
    }

    return 1;
}

/**
 * os memblock get 
 *  
 * Get a memory block from a memory pool 
 * 
 * @param mp Pointer to the memory pool
 * 
 * @return void* Pointer to block if available; NULL otherwise
 */
void *
os_memblock_get(struct os_mempool *mp)
{
    os_sr_t sr;
    struct os_memblock *block;

    /* Check to make sure they passed in a memory pool (or something) */
    block = NULL;
    if (mp) {
        OS_ENTER_CRITICAL(sr);
        /* Check for any free */
        if (mp->mp_num_free) {
            /* Get a free block */
            block = list_first_entry(&mp->mp_hdr, os_memblock, mb_node);

            /* Free the block */
            list_del(&block->mb_node);

            /* Decrement number free by 1 */
            mp->mp_num_free--;
        }
        OS_EXIT_CRITICAL(sr);
    }

    return (void *)block;
}

/**
 * os memblock put 
 *  
 * Puts the memory block back into the pool 
 * 
 * @param mp Pointer to memory pool
 * @param block_addr Pointer to memory block
 * 
 * @return os_error_t 
 */
os_error_t
os_memblock_put(struct os_mempool *mp, void *block_addr)
{
    os_sr_t sr;
    struct os_memblock *block;

    /* Make sure parameters are valid */
    if ((mp == NULL) || (block_addr == NULL)) {
        return OS_INVALID_PARM;
    }

    /* Check that the block we are freeing is a valid block! */
    if (!os_memblock_from(mp, block_addr)) {
        return OS_INVALID_PARM;
    }

    block = (struct os_memblock *)block_addr;
    OS_ENTER_CRITICAL(sr);
    
    /* Chain current free list pointer to this block; make this block head */
    list_add_tail(&block->mb_node, &mp->mp_hdr);

    /* XXX: Should we check that the number free <= number blocks? */
    /* Increment number free */
    mp->mp_num_free++;

    OS_EXIT_CRITICAL(sr);

    return OS_OK;
}

