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
#define _GNU_SOURCE
#include <unistd.h>
#include <dlfcn.h>
#include <pthread.h>
#include "btff.h"

static struct root root = { PTHREAD_MUTEX_INITIALIZER, 0, NULL, NULL };
static struct btff* btff = NULL;

void *malloc(size_t size)
{
	if(0 >= size)
		return NULL;
	else
	{
		struct stack stack[STACK];
		void* ptr;
		pthread_mutex_lock(&root.mutex);
		if(!btff)
		{
			posix_memalign((void**)&btff, 0, 0);
			btff->root = &root;
		}
		stack[ROOT].available = root.available;
		stack[ROOT].node = root.node;
		stack[LIST].node = root.list;
		ptr = btff->malloc(stack, size);
		if(root.available != stack[ROOT].available)
			root.available = stack[ROOT].available;
		if(root.list != stack[LIST].node)
			root.list = stack[LIST].node;
		pthread_mutex_unlock(&root.mutex);
		return ptr;
	}
}

void free(void *ptr)
{
	if(!ptr || ptr == btff)
		return;
	else
	{
		struct stack stack[STACK];
		pthread_mutex_lock(&root.mutex);
		if(!btff)
		{
			posix_memalign((void**)&btff, 0, 0);
			btff->root = &root;
		}
		stack[ROOT].available = root.available;
		stack[ROOT].node = root.node;
		stack[LIST].node = root.list;
		btff->free(stack, ptr);
		if(root.available != stack[ROOT].available)
			root.available = stack[ROOT].available;
		if(root.list != stack[LIST].node)
			root.list = stack[LIST].node;
		pthread_mutex_unlock(&root.mutex);
	}
}

void *realloc(void *ptr, size_t size)
{
	if(!ptr && size <= 0)
		return NULL;
	else
	if(ptr == btff)
		return NULL;
	else
	{
		struct stack stack[STACK];
		pthread_mutex_lock(&root.mutex);
		if(!btff)
		{
			posix_memalign((void**)&btff, 0, 0);
			btff->root = &root;
		}
		stack[ROOT].available = root.available;
		stack[ROOT].node = root.node;
		stack[LIST].node = root.list;
		if(ptr)
		{
			if(0 < size)
			{
				size_t old_size;
				void* new_ptr;
				new_ptr = btff->realloc(stack, ptr, &old_size, size);
				if(new_ptr != ptr)
				{
					btff->memmove(new_ptr, ptr, old_size);
					ptr = new_ptr;
				}
			}
			else
			{
				btff->free(stack, ptr);
				ptr = NULL;
			}
		}
		else
		if(0 < size)
			ptr = btff->malloc(stack, size);
		if(root.available != stack[ROOT].available)
			root.available = stack[ROOT].available;
		if(root.list != stack[LIST].node)
			root.list = stack[LIST].node;
		pthread_mutex_unlock(&root.mutex);
		return ptr;
	}
}

static pid_t (*pfork)(void);

pid_t fork(void)
{
	pid_t pid;
    pthread_mutex_lock(&root.mutex);
    pid = pfork();
    pthread_mutex_unlock(&root.mutex);
	return pid;
}

void _init(void)
{
    pfork = dlsym(RTLD_NEXT, "fork");
}

