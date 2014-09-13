/*------------------------------------------------------------------------------

Copyright (c) 2014, Young H. Song song@youngho.net
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:
1. Redistributions of source code must retain the above copyright
   notice, this list of conditions and the following disclaimer.
2. Redistributions in binary form must reproduce the above copyright
   notice, this list of conditions and the following disclaimer in the
   documentation and/or other materials provided with the distribution.
3. All advertising materials mentioning features or use of this software
   must display the following acknowledgement:
   This product includes software developed by the Young H. Song.
4. Neither the name of the Young H. Song nor the
   names of its contributors may be used to endorse or promote products
   derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY Young H. Song ''AS IS'' AND ANY
EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL Young H. Song BE LIABLE FOR ANY
DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

------------------------------------------------------------------------------*/
/* B Tree First Fit Memory Allocator */
#ifndef __btff_h__
#define __btff_h__ __btff_h__

#include <pthread.h>
#include <stdlib.h>

struct root
{
	pthread_mutex_t mutex;
	unsigned long available;
	void* node;
	void* list;
};

enum { LEAF = 30, LIST, STACK };

#define LEVEL(p) ((int)((p) ? (((unsigned long)((struct node*)(p))->level) < LEAF ? ((struct node*)(p))->level : LEAF) : LEAF))
#define ROOT (btff->root ? LEVEL(btff->root->node) : LEAF) 

struct stack
{
    unsigned long available;
    void* node;
    int child;
};

struct btff
{
	struct root* root;
	void * (*memmove)(register void *dst, register void *src, register int len);
	int (*brk)(void *addr);
	void* (*sbrk)(intptr_t increment);
    void* (*malloc)(struct stack* stack, size_t size);
    void (*free)(struct stack* stack, void *ptr);
    void* (*realloc)(struct stack* stack, void *ptr, size_t* old_size, size_t size);
	void* (*memalign)(struct stack* stack, size_t alignment, size_t size);
	void (*sanity_check)(void* p, int level, void* address_end);
	void (*available_check)(void* root, int level);
};

#define NODE_SIZE 7
#define NODE_MIDDLE (NODE_SIZE / 2)

struct node
{
    int level;
    int size;
    void* address[NODE_SIZE];
    unsigned long available[NODE_SIZE];
};

#define LEAF_SIZE (64 - sizeof(void*) - sizeof(char))
#define LEAF_MIDDLE (LEAF_SIZE / 2)
#define AVAILABLE 0x01
#define ALIGNMENT 8

struct leaf
{
    void* address;
    char size;
    unsigned char available[LEAF_SIZE];
} __attribute__ ((__packed__));

#endif/*__btff_h__*/
