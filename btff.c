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
#include <stdlib.h>
#include <malloc.h>
#include <errno.h>
#include <unistd.h>
#include <sys/mman.h>
#include <pthread.h>
#include "btff.h"

static void *btff_memmove(register void *dest, register void *src, register int n);
static void *btff_malloc(struct stack* stack, size_t size);
static void btff_free(struct stack* stack, void *ptr);
static void *btff_realloc(struct stack* stack, void *ptr, size_t* old_size, size_t size);
static void *brk_memalign(struct stack* stack, size_t alignment, size_t size);
static void sanity_check(void* p, int level, void* address_end);
static void available_check(void* root, int level);
static struct btff btff[1] = { { NULL, btff_memmove, brk, sbrk, btff_malloc, btff_free, btff_realloc, brk_memalign, sanity_check, available_check } };

int posix_memalign(void **memptr, size_t alignment, size_t size)
{
	if(0 == alignment && 0 == size)
	{
		*memptr = btff;
		return EINVAL;
	}
	else
	{
		size_t power;
		for(power = 1 << 31; power >= sizeof(void*); power >>= 1)
			if(alignment == power)
				break;
		if(power < sizeof(void*))
			return EINVAL;
		else
		if(0 == size)
		{
			*memptr = NULL;
			return 0;
		}
		else
		if(!btff->root)
			return ENOMEM;
		else
		{
			struct stack stack[STACK];
			void* ptr;
			pthread_mutex_lock(&btff->root->mutex);
			stack[ROOT].available = btff->root->available;
			stack[ROOT].node = btff->root->node;
			stack[LIST].node = btff->root->list;
			ptr = btff->memalign(stack, alignment, size);
			if(btff->root->available != stack[ROOT].available)
				btff->root->available = stack[ROOT].available;
			if(btff->root->list != stack[LIST].node)
				btff->root->list = stack[LIST].node;
			pthread_mutex_unlock(&btff->root->mutex);
			if(!ptr)
				return ENOMEM;
			*memptr = ptr;
		}
	}
	return ENOMEM;
}

#define DEBUG do { } while(0)

/*----------------------------------------------------------------------------*/
void btff_perror(const char *s)
{
	for( ; *s; s++)
		write(2, s, 1);
	write(1, "\n", 1);
}

void btff_nerror(unsigned long n)
{
	int i;
	char buffer[80];

	for(i = 0; n > 0 ; i++, n /= 10)
		buffer[i] = '0' + (n % 10);
	for(i--; i >= 0; i--)
		write(2, buffer + i, 1);
	write(1, "\n", 1);
}

#define GOTO_LEAF_SEARCH goto LEAF_SEARCH

static inline void *btff_memmove(register void *dest, register void *src, register int n)
{
	if((0x03 & (unsigned long)dest) || (0x03 & (unsigned long)src) || (0x03 & n))
	{
		if(dest < src || src + n <= dest)
			for(n--; n >= 0; n--, dest++, src++)
				(*(unsigned char*)dest) = (*(unsigned char*)src);
		else
		if(src < dest)
			for(n--; n >= 0; n--)
				(*(unsigned char*)(dest + n)) = (*(unsigned char*)(src + n));
	}
	else
	{	
		if(dest < src || src + n <= dest)
			for(n -= 4; n >= 0; n -= 4, dest += 4, src += 4)
				(*(unsigned long*)dest) = (*(unsigned long*)src);
		else
		if(src < dest)
			for(n -= 4; n >= 0; n -=4)
				(*(unsigned long*)(dest + n)) = (*(unsigned long*)(src + n));
	}
	return dest;
}

#define btff_memcpy(dest, src, n) btff_memmove(dest, src, n)
#define GOTO_ERROR do { btff_perror(__FILE__);	btff_nerror(__LINE__); btff_perror(__FUNCTION__); goto ERROR; } while(0)

/*----------------------------------------------------------------------------*/

struct list
{
	struct list* next;
};

static inline void delete64byte(register struct stack* stack, register void* delete)
{
	register struct list* list = stack[LIST].node;
	((struct list*)delete)->next = list;
	list = delete;
	stack[LIST].node = list;
}

static inline void* new64byte(register struct stack* stack)
{
	register struct list* list = stack[LIST].node;
	register void* new;
	if(!list)
	{
		static long size = 0;
		register int i;
		if(!size)
			size = sysconf(_SC_PAGESIZE);
		if(!(new = mmap(NULL, size, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0)))
		{
			btff_perror(sys_errlist[errno]);
			exit(EXIT_FAILURE);
		}
		for(i = 0; i < size; i += 64)
			delete64byte(stack, new + i);
		list = stack[LIST].node;
	}
	new = list;
	list = list->next;
	stack[LIST].node = list;
	return new;
}

#define new_node(stack) ((struct node*)new64byte(stack))
#define delete_node(stack, node) delete64byte(stack, (void*)node)
#define new_leaf(stack) ((struct leaf*)new64byte(stack))
#define delete_leaf(stack, leaf) delete64byte(stack, (void*)leaf)

/*----------------------------------------------------------------------------*/

static inline unsigned char* _leaf_next(register unsigned char* p, unsigned long* r_size)
{
	register int i;
	register unsigned long size;
    {
        register unsigned char mask;
        register unsigned char bit;

        for(mask = 0, bit = 0x80, i = 0;
            bit & *p;
            mask |= bit, bit >>= 1, i++);
        size = (unsigned long)(~mask & *p);
    }
	for(p++; i > 0; p++, i--)
	{
		size <<= 8;
		size |= (unsigned long)*p;
	}
    size >>= 1;
    size <<= 2;
    if(r_size)
        *r_size = size;
    return p;
}

#define leaf_next(begin, available) ((0x80 & *(begin)) ? _leaf_next(begin, (available)) : (((*(available) = *(begin)) && (*(available) >>= 1) && (*(available) <<= 2)) ? ((begin) + 1) : (begin + 1)))

static void leaf_print(struct leaf* leaf)
{
	register unsigned char* middle;
	register unsigned char* end;
	unsigned char* leaf_end;
	register void* address;
	unsigned long available;

	for(address = leaf->address, middle = leaf->available, leaf_end = leaf->available + (int)leaf->size;
		middle < leaf_end;
		address += available, middle = end)
	{
		end = leaf_next(middle, &available);
		printf("address: %08x", (unsigned int)address);
		if(end[-1] & AVAILABLE)
			printf("     available: %d\n", (int)available);
		else
			printf(" not available: %d\n", (int)available);
	}
}

static unsigned char* leaf_last(struct leaf* leaf, unsigned long* r_max, unsigned char** r_begin, void** r_address, unsigned long* r_size)
{
    register unsigned char* p;
    register unsigned char* n = NULL;
    register void* address;
    unsigned long size = 0;
	register unsigned char* leaf_end;
	unsigned long max = 0;

    address = leaf->address;
    for(p = leaf->available, leaf_end = leaf->available + (int)leaf->size; p < leaf_end; p = n)
    {
		n = leaf_next(p, &size);
		if(leaf_end <= n)
			break;
        address += size;
		if(n[-1] & AVAILABLE)
			if(max < size)
				max = size;
    }
	if(r_begin)
		*r_begin = p;
	if(r_max)
		*r_max = max;
    if(r_address)
        *r_address = address;
    if(r_size)
        *r_size = size;
    return n;
}

static void* leaf_address_end(struct leaf* leaf)
{
	void* address;
	unsigned long size;
	leaf_last(leaf, NULL, NULL, &address, &size);
	return address + size;
}

static unsigned char* leaf_update(struct leaf* leaf, unsigned char* begin, unsigned char* end, unsigned char* tmp_begin, unsigned char* tmp_end)
{
	unsigned char* new_end = begin + (tmp_end - tmp_begin);
	btff_memmove(new_end, end, leaf->size - (end - leaf->available));
	btff_memcpy(begin, tmp_begin, tmp_end - tmp_begin);
	leaf->size -= end - begin;
	leaf->size += tmp_end - tmp_begin;
	return new_end;
}

static unsigned is_overflow(struct stack* stack, int level)
{
	if(level < LEAF)
	{
		struct node* node = stack[level].node;
		if(NODE_SIZE <= node->size)
			return 1;
	}
	else
	{
		struct leaf* leaf = stack[LEAF].node;
		if(LEAF_SIZE <= leaf->size)
			return 1;
	}
	return 0;
}

static inline int right_down(struct stack* stack, int level, void (*split)(struct stack*, int, int), int* r_i)
{
	int i = *r_i;
	struct node* node = stack[level].node;
RIGHT:
	stack[level].child = i + 1;
	stack[level + 1].available = node->available[i + 1];
	stack[level + 1].node = node->address[i + 1];
	if(split && is_overflow(stack, level + 1))
	{
		split(stack, level, i + 1);
		goto RIGHT;
	}
	*r_i = i;
	return level + 1;
}

static inline int left_down(struct stack* stack, int level, void (*split)(struct stack*, int, int), int* r_i)
{
	int i = *r_i;
	struct node* node = stack[level].node;
LEFT:
	stack[level].child = i - 1;
	stack[level + 1].available = node->available[i - 1];
	stack[level + 1].node = node->address[i - 1];
	if(split && is_overflow(stack, level + 1))
	{
		split(stack, level, i - 1);
		i += 2;
		goto LEFT;
	}
	*r_i = i;
	return level + 1;
}

static inline struct leaf* far_left_leaf(struct stack* stack, int level, void (*split)(struct stack*, int, int))
{
	for( ; level < LEAF; level++)
	{
		struct node* node = stack[level].node;
	REPEAT:
		stack[level].child = 0;
		stack[level + 1].node = node->address[0];
		stack[level + 1].available = node->available[0];
		if(split && is_overflow(stack, level + 1))
		{
			split(stack, level, 0);
			goto REPEAT;
		}
	}
	return stack[LEAF].node;
}

static inline struct leaf* far_right_leaf(struct stack* stack, int level, void (*split)(struct stack*, int, int))
{
	for( ; level < LEAF; level++)
	{
		struct node* node = stack[level].node;
	REPEAT:
		stack[level].child = node->size - 1;
		stack[level + 1].node = node->address[node->size - 1];
		stack[level + 1].available = node->available[node->size - 1];
		if(split && is_overflow(stack, level + 1))
		{
			split(stack, level, node->size - 1);
			goto REPEAT;
		}
	}
	return stack[LEAF].node;
}

#define right_leaf(stack, level, split, r_i) far_left_leaf(stack, right_down(stack, level, split, r_i), split)
#define left_leaf(level, split, r_i) far_right_leaf(stack, left_down(stack, level, split, r_i), split)

static inline int right_node(struct stack* stack, int* r_i)
{
	int level;
	for(level = LEAF - 1; level >= ROOT; level--)
	{
		struct node* node = stack[level].node;
		int i = stack[level].child;
		if(i + 1 < node->size)
		{
			*r_i = i + 1;
			return level;
		}
	}
	*r_i = -1;
	return -1;
}

static inline int left_node(struct stack* stack, int* r_i)
{
	int level;
	for(level = LEAF - 1; level >= ROOT; level--)
	{
		int i = stack[level].child;
		if(0 <= i - 1)
		{
			*r_i = i - 1;
			return level;
		}
	}
	*r_i = -1;
	return -1;
}

static inline void leaf_overflow(struct leaf* leaf)
{
	int j;
	for(j = leaf->size; j < LEAF_SIZE; j++)
		leaf->available[j] = 0x00;
	leaf->size = LEAF_SIZE;
}

static int node_search_available(struct stack* stack, int level, size_t size, void (*split)(struct stack*, int, int), int* r_i)
{
	for( ; level < LEAF; level++)
	{
		int i;
		struct node* node;
		node = stack[level].node;
		for(i = 0; i < node->size; )
		{
		EVEN:
			if(size <= node->available[i])
				break;
			i++;
		/* ODD */
			if(size <= node->available[i])
			{
				if(r_i)
					*r_i = i;
				return level;
			}
			i++;
		}
		if(i == node->size)
			return -1;
		stack[level].child = i;
		stack[level + 1].available = node->available[i];
		stack[level + 1].node = node->address[i];
		if(split && is_overflow(stack, level + 1))
		{
			split(stack, level, i);
			goto EVEN;	
		}
	}
	return LEAF;
}

static int node_search_address(struct stack* stack, int level, void* ptr, void (*split)(struct stack*, int, int), int* r_i)
{
	int i;
	for( ; level < LEAF; level++)
	{
		struct node* node;
		node = stack[level].node;
		for(i = 1; i < node->size; i+= 2)
		{
		RESUME:
			if(ptr == node->address[i])
			{
				if(0 < node->available[i])
					return -1;
				if(r_i)
					*r_i = i;
				return level;
			}
			else
			if(ptr < node->address[i])
				break;
		}
		stack[level].child = i - 1;
		stack[level + 1].available = node->available[i - 1];
		stack[level + 1].node = node->address[i - 1];
		if(split)
		{
			if(is_overflow(stack, level + 1))
			{
				split(stack, level, i - 1);
				goto RESUME;
			}
		}
	}
	return LEAF;
}

static unsigned char* leaf_search_available(struct leaf* leaf, unsigned long size, unsigned long* r_max, unsigned char** r_begin, void** r_address, unsigned long* r_available)
{
	register unsigned char* begin;
	register unsigned char* end;
	unsigned char* leaf_end;
	register void* address;
	unsigned long available;
	unsigned long max;
	if(!leaf)
	{
	DEBUG;
		return NULL;
	}
	for(begin = leaf->available, leaf_end = leaf->available + (int)leaf->size, address = leaf->address, max = 0;
		begin < leaf_end;
		begin = end, address += available)
	{
		end = leaf_next(begin, &available);
		if(end[-1] & AVAILABLE)
		{
			if(size <= available)
			{
				if(r_max)
					*r_max = max;
				if(r_begin)
					*r_begin = begin;
				if(r_address)
					*r_address = address;
				if(r_available)
					*r_available = available;
				return end;
			}
			else
			if(max < available)
				max = available;
		}
	}
	return NULL;
} 

static unsigned char* leaf_search_address(struct leaf* leaf, void* ptr, unsigned char** r_left, unsigned char** r_middle, void** r_address, unsigned long* r_available)
{
	register unsigned char* left;
	register unsigned char* middle;
	register unsigned char* end;
	unsigned char* leaf_end;
	register void* address;
	unsigned long available;

	for(address = leaf->address, left = NULL, middle = leaf->available, leaf_end = leaf->available + (int)leaf->size;
		middle < leaf_end;
		address += available, left = middle, middle = end)
	{
		end = leaf_next(middle, &available);
		if(ptr == address)
		{
			if(r_left)
				*r_left = left;
			if(r_middle)
				*r_middle = middle;
			if(r_address)
				*r_address = address;
			if(r_available)
				*r_available = available;
			return end;
		}
	}
	return NULL;
}

static unsigned char* leaf_append(unsigned char* begin, register unsigned long size)
{
	register int i, j;
	register unsigned char tmp;
	unsigned char* end;
	for(i = 0, size >>= 1; size; i++, size >>= 8)
		begin[i] = (unsigned char)(0xFF & size);
	begin[i] = 0x00;
	for(j = 0, tmp = 0x80; j < i; j++, tmp >>= 1)
		begin[i] |= tmp;
	if(0x00 == (begin[i] & begin[i - 1]))
	{
		begin[i] <<= 1;
		begin[i - 1] |= begin[i];
		i--;
	}
	end = begin + i + 1;
	for(j= 0; j < i; j++, i--)
	{
		tmp = begin[i];
		begin[i] = begin[j];
		begin[j] = tmp;
	}
	return end;
}

static inline unsigned long node_available(register unsigned long* available, register int size)
{
	register unsigned long max = 0;
	for(size--; size >= 0; size--, available++)
		if(max < *available)
			max = *available;
	return max;
}

static unsigned long leaf_available(unsigned char* available, int size)
{
	unsigned long max = 0;
	register unsigned char* begin;
	register unsigned char* end;
	unsigned long value;
	unsigned char* available_end;

	for(begin = available, available_end = available + size; begin < available_end; begin = end)
	{
		end = leaf_next(begin, &value);
		if(end[-1] & AVAILABLE)
			if(max < value)
				max = value;
	}
	return max;
}

static void node_split(struct stack* stack, int level, int i)
{
	struct node* parent = stack[level].node;
	btff_memmove(parent->address + i + 3, parent->address + i + 1, sizeof(parent->address[0]) * (parent->size - (i + 1)));
	btff_memmove(parent->available + i + 3, parent->available + i + 1, sizeof(parent->available[0]) * (parent->size - (i + 1)));
	parent->size += 2;
	parent->address[i + 1] = parent->address[i + 2] = NULL;
	parent->available[i + 1] = parent->available[i + 2] = 0;

	if(level + 1 < LEAF)
	{
		struct node* left;
		struct node* right;
	DEBUG;
		left = parent->address[i];
		left->size = NODE_MIDDLE;
		parent->available[i] = node_available(left->available, NODE_MIDDLE);

		parent->address[i + 1] = left->address[NODE_MIDDLE];
		parent->available[i + 1] = left->available[NODE_MIDDLE];

		right = parent->address[i + 2] = new_node(stack);
		right->level = level + 1;
		right->size = NODE_MIDDLE;
		btff_memcpy(right->address, left->address + NODE_MIDDLE + 1, sizeof(right->address[0]) * NODE_MIDDLE);
		btff_memcpy(right->available, left->available + NODE_MIDDLE + 1, sizeof(right->available[0]) * NODE_MIDDLE);
		parent->available[i + 2] = node_available(right->available, NODE_MIDDLE);
	}
	else
	{
		struct leaf* left;
		struct leaf* right;
		void* address;
		register unsigned char* begin;
		register unsigned char* end;
		unsigned char* middle;
		unsigned long value;
		unsigned long max;
	DEBUG;
		left = parent->address[i];
		for(begin = left->available, middle = begin + LEAF_MIDDLE, address = left->address, max = 0; ; begin = end, address += value)
		{
			end = leaf_next(begin, &value);
			if(middle < end)
			{
				middle = begin;
				break;
			}
			if(end[-1] & AVAILABLE)
				if(max < value)
					max = value;
		}
		left->size = middle - left->available;
		parent->available[i] = max;

		parent->address[i + 1] = address;
		parent->available[i + 1] = (end[-1] & AVAILABLE) ? value : 0;

		right = parent->address[i + 2] = new_leaf(stack);
		right->address = address + value;
		right->size = 0;
		for(begin = end, max = 0; begin < left->available + LEAF_SIZE; begin = end)
		{
			end = leaf_next(begin, &value);
			if(0 < value)
			{
				if(0x80 & *begin)
					leaf_update(right, right->available + (int)right->size, right->available + (int)right->size, begin, end);
				else
				{
					right->available[(int)right->size] = *begin;
					right->size++;
				}
			}
			if(end[-1] & AVAILABLE)
				if(max < value)
					max = value;
		}
		parent->available[i + 2] = max;
	}
}

static unsigned node_merge(struct stack* stack, int level, int middle)
{
	struct node* parent = stack[level].node;
	if(level + 1 < LEAF)
	{
		struct node* left;
		struct node* right;
		int i;

		left = parent->address[middle - 1];
		right = parent->address[middle + 1];
		if(NODE_SIZE < left->size + 1 + right->size)
			return 0;
		left->address[left->size] = parent->address[middle];
		left->available[left->size] = parent->available[middle];
		left->size++;

		for(i = 0; i < right->size; i++)
		{
			left->address[left->size] = right->address[i];
			left->available[left->size] = right->available[i];
			left->size++;
		}
		delete_node(stack, right);
	}
	else
	{
		struct leaf* left;
		struct leaf* right;
		unsigned char tmp[12];
		unsigned char* begin;
		unsigned char* end;

		left = parent->address[middle - 1];
		right = parent->address[middle + 1];
		begin = tmp;
		end = leaf_append(begin, right->address - parent->address[middle]);
		if(0 < parent->available[middle])
		{
			end[-1] |= AVAILABLE;
		}
		if(LEAF_SIZE < left->size + (end - begin) + right->size)
			return 0;
		leaf_update(left, left->available + (int)left->size, left->available + (int)left->size, begin, end);
		leaf_update(left, left->available + (int)left->size, left->available + (int)left->size, right->available, right->available + (int)right->size);
	DEBUG;
		delete_leaf(stack, right);
	}
	parent->available[middle - 1] = node_available(parent->available + middle - 1, 3);
	btff_memmove(parent->address + middle, parent->address + middle + 2, sizeof(parent->address[0]) * (parent->size - (middle + 2)));
	btff_memmove(parent->available + middle, parent->available + middle + 2, sizeof(parent->available[0]) * (parent->size - (middle + 2)));
	parent->size -= 2;
	parent->address[parent->size] = parent->address[parent->size + 1] = NULL;
	parent->available[parent->size] = parent->available[parent->size + 1] = 0;
	return 1;
}

static void node_rebalance(struct stack* stack, int level, int middle)
{
	struct node* parent = stack[level].node;
	if(level + 1 < LEAF)
	{
		struct node* left = parent->address[middle - 1];
		struct node* right = parent->address[middle + 1];
	DEBUG;
		if(left->size + 4 <= right->size)
		{
			int i;
		DEBUG;
			for(i = 0; left->size < right->size && 4 <= right->size; i += 2, right->size -= 2)
			{
				left->address[left->size] = parent->address[middle];
				left->available[left->size] = parent->available[middle];
				if(parent->available[middle - 1] < left->available[left->size])
					parent->available[middle - 1] = left->available[left->size];
				left->size++;
				left->address[left->size] = right->address[i];
				left->available[left->size] = right->available[i];
				if(parent->available[middle - 1] < left->available[left->size])
					parent->available[middle - 1] = left->available[left->size];
				left->size++;
				parent->address[middle] = right->address[i + 1];
				parent->available[middle] = right->available[i + 1];
			}
			btff_memmove(right->address, right->address + i, sizeof(right->address[0]) * right->size);
			btff_memmove(right->available, right->available + i, sizeof(right->available[0]) * right->size);
			parent->available[middle + 1] = node_available(right->available, right->size);
		}
		else
		if(right->size + 4 <= left->size)
		{
			int delta;
			int l;
		DEBUG;
			delta = left->size - right->size;
			delta /= 2;
			if(delta & 1)
				delta--;
			btff_memmove(right->address + delta, right->address, sizeof(right->address[0]) * right->size);
			btff_memmove(right->available + delta, right->available, sizeof(right->available[0]) * right->size);
			delta--;
			right->address[delta] = parent->address[middle];
			right->available[delta] = parent->available[middle];
			if(parent->available[middle + 1] < right->available[delta])
				parent->available[middle + 1] = right->available[delta];
			for(delta--, l = left->size - 1; delta >= 0; delta--, l--)
			{
				right->address[delta] = left->address[l];
				right->available[delta] = left->available[l];
				if(parent->available[middle + 1] < right->available[delta])
					parent->available[middle + 1] = right->available[delta];
			}
			parent->address[middle] = left->address[l];
			parent->available[middle] = left->available[l];
			delta = left->size - right->size;
			delta /= 2;
			if(delta & 1)
				delta--;
			left->size -= delta;
			right->size += delta;
			parent->available[middle - 1] = node_available(left->available, left->size);
		}
	}
	else
	{
		struct leaf* left = parent->address[middle - 1];
		struct leaf* right = parent->address[middle + 1];
		if(left->size + 10 <= right->size)
		{
			register void* address;
			unsigned long available;
			register unsigned char* begin;
			register unsigned char* end;
			unsigned char tmp[12];
		DEBUG;
			for(begin = right->available, address = right->address; (begin < right->available + (int)right->size) && (left->size < right->size); begin = end)
			{
				end = leaf_append(tmp, address - parent->address[middle]);
				if(0 < parent->available[middle])
				{
					end[-1] |= AVAILABLE;
					if(parent->available[middle - 1] < parent->available[middle])
						parent->available[middle - 1] = parent->available[middle];
				}
				leaf_update(left, left->available + (int)left->size, left->available + (int)left->size, tmp, end);

				parent->address[middle] = address;
				end = leaf_next(begin, &available);
				parent->available[middle] = (end[-1] & AVAILABLE) ? available : 0;

				address += available;
				right->size -= end - begin;
			}
			right->address = address;
			btff_memmove(right->available, begin, right->size);
			parent->available[middle + 1] = leaf_available(right->available, right->size);
		}
		else
		if(right->size + 10 <= left->size)
		{
			register void* address;
			unsigned long available;
			register unsigned char* begin;
			unsigned char middle_begin[12];
			unsigned char* middle_end;
			register unsigned char* end;
			unsigned char* p;
			unsigned char* right_middle_end;
			register int left_size;
			int middle_size;
			register int right_size;
			unsigned long left_available;
		DEBUG;
			address = left->address;
			begin = left->available;
			end = leaf_next(begin, &available);

			middle_end = leaf_append(middle_begin, right->address - parent->address[middle]);
			if(0 < parent->available[middle])
				middle_end[-1] |= AVAILABLE;
			middle_size = middle_end - middle_begin;

			right_size = left->size - (end - begin) + middle_size + right->size;
			middle_size = end - begin;
			left_size = 0;

			left_available = 0;
			while((begin < left->available + (int)left->size) && (left_size < right_size))
			{
				left_size += middle_size;
				if(end[-1] & AVAILABLE)
					if(left_available < available)
						left_available = available;

				address += available;
				begin = end;
				end = leaf_next(begin, &available);
				middle_size = end - begin;

				right_size -= middle_size;
			}
			if(left->size <= left_size)
				goto RETURN;
			parent->available[middle - 1] = left_available;

			parent->address[middle] = address;
			parent->available[middle] = (end[-1] & AVAILABLE) ? available : 0;

			right->address = parent->address[middle] + available;
			right_middle_end = right->available;
			right_middle_end += left->size - (end - left->available);
			right_middle_end += middle_end - middle_begin;
			p = right_middle_end;
			btff_memmove(p, right->available, right->size);
			p -= middle_end - middle_begin;
			btff_memcpy(p, middle_begin, middle_end - middle_begin);
			btff_memcpy(right->available, end, p - right->available);
			if(parent->available[middle + 1] < (available = leaf_available(right->available, right_middle_end - right->available)))
				parent->available[middle + 1] = available;
			left->size = left_size;
			right->size = right_size;
		}
RETURN:
		return;
	}
}

static unsigned is_underflow(struct stack* stack, int level)
{
	if(level < LEAF)
	{
		struct node* node = stack[level].node;
		if(node->size <= NODE_MIDDLE)
			return 1;
	}
	else
	{
		struct leaf* leaf = stack[LEAF].node;
		if(leaf->size <= LEAF_MIDDLE)
			return 1;
	}
	return 0;
}

static void available_decrease(struct stack* stack, int level)
{
	for( ; level >= ROOT; level--)
	{
		struct node* node;
		int i;
		unsigned long old_available;

		node = stack[level].node;
		i = stack[level].child;
		old_available = node->available[i];
		node->available[i] = stack[level + 1].available;
		if(stack[level].available <= old_available)
			stack[level].available = node_available(node->available, node->size);
		else
			return;
	}
}

static void available_increase(struct stack* stack, int level)
{	
	for( ; level >= ROOT; level--)
	{
		unsigned long available;
		struct node* node;
		int i;

		available = stack[level + 1].available;
		node = stack[level].node;
		i = stack[level].child;
		node->available[i] = available;
		if(stack[level].available < available)
			stack[level].available = available;
		else
			return;
	}
}

static int overflow(struct stack* stack, int level)
{
	struct node* node;
	for( ; level >= ROOT; level--)
		if(!is_overflow(stack, level))
			return level;
	node = new_node(stack);
	node->level = ROOT - 1;
	node->address[0] = stack[ROOT].node;
	node->available[0] = stack[ROOT].available;
	node->size = 1;
	btff->root->node = node;
	stack[ROOT].node = node;
	stack[ROOT].available = node_available(node->available, node->size);
	stack[ROOT].child = 0;
	return ROOT;
}

static int rebalance(struct stack* stack, int level)
{	
	struct node* node;
	while(level > ROOT)
	{
		/* int left; */
		int middle;
		/* int right; */
		int i;
		if(!is_underflow(stack, level))
			return level;
		level--;
		node = stack[level].node;
		i = stack[level].child;	
		if(i < 0 || node->size <= i)
		{
		DEBUG;
			exit(EXIT_FAILURE);
			return level;
		}
		else
		if(i + 2 < node->size)
		{
		DEBUG;
			/* left = i; */
			middle = i + 1;
			/* right = i + 2; */
		}
		else
		if(0 <= i - 2)
		{
		DEBUG;
			/* left = i - 2; */
			middle = i - 1;
			/* right = i; */
		}
		else
			return level;
		if(node_merge(stack, level, middle))
		{
		DEBUG;
			stack[level].child = middle - 1;
			stack[level + 1].node = node->address[middle - 1];
			stack[level + 1].available = node->available[middle - 1];
		}
		else
		{
		DEBUG;
			node_rebalance(stack, level, middle);
			stack[level].child = - 1;
			stack[level + 1].node = NULL;
			stack[level + 1].available = 0;
		}
	}
	if(ROOT < LEAF)
	{
		node = stack[ROOT].node;
		if(node->size <= 1)
		{
			if(LEVEL(node->address[0]) != ROOT + 1)
				GOTO_ERROR;
			btff->root->node = node->address[0];
			delete_node(stack, node);
		}
	}
	if(ROOT == LEAF)
	{
		struct leaf* leaf = stack[LEAF].node;
		if(leaf->size == 0)
		{
		DEBUG;
			btff->root->node = NULL;
			delete_leaf(stack, leaf);
			stack[LEAF].available = 0;
			stack[LEAF].node = NULL;
			stack[LEAF].child = -1;
		}
	}
	return ROOT;
ERROR:
	btff_perror(__FUNCTION__);
	if(0 != errno)
		btff_perror(sys_errlist[errno]);
	exit(EXIT_FAILURE);
	return -1;
}

/*----------------------------------------------------------------------------*/

static void brk_adjust(struct stack* stack, void* ptr)
{
	int level;
	struct leaf* leaf;
	void* address;
	unsigned char tmp[12];
	unsigned char* begin;
	unsigned char* end;
	void (*split)(struct stack*, int, int);
	split = NULL;
	if(!stack[ROOT].node)
	{
		if(ROOT != LEAF)
			GOTO_ERROR;
		address = btff->sbrk(0);
		if(address < (void*)LEAF)
			address = (void*)LEAF;
		while(0x00000003 & (unsigned long)address)
			address++;
		if(-1 == btff->brk(address))
			GOTO_ERROR;
		leaf = new_leaf(stack);
		leaf->address = address;
		leaf->size = 0;
		btff->root->node = leaf;
		stack[LEAF].node = leaf;
		stack[LEAF].available = 0;
	}
	else
	{	
		level = ROOT;
	REPEAT:
		leaf = far_right_leaf(stack, level, split);
		address = btff->sbrk(0);
	}
	begin = tmp;
	end = leaf_append(begin, ptr - address);
	if(leaf->size + (end - begin) <= LEAF_SIZE)
	{
		if(-1 == btff->brk(ptr))
			GOTO_ERROR;
		leaf_update(leaf, leaf->available + (int)leaf->size, leaf->available + (int)leaf->size, begin, end);
	}
	else
	{	
	DEBUG;
		leaf_overflow(leaf);
		level = overflow(stack, LEAF);
		split = node_split;
		goto REPEAT;
	}
	return;
ERROR:
	btff_perror(__FUNCTION__);
	if(0 != errno)
		btff_perror(sys_errlist[errno]);
}

void *brk_memalign(struct stack* stack, size_t alignment, size_t size)
{
	struct leaf* leaf;
	void* address;
	unsigned char tmp[12];
	unsigned long left_available;
	unsigned char* begin;
	unsigned char* end;
	unsigned long available;
	if(!stack[ROOT].node)
	{
		if(ROOT != LEAF)
			GOTO_ERROR;
		address = btff->sbrk(0);
		if(address < (void*)LEAF)
			address = (void*)LEAF;
		while((ALIGNMENT - 1) & (unsigned long)address)
			address++;
		if(-1 == btff->brk(address))
			GOTO_ERROR;
		leaf = new_leaf(stack);
		leaf->address = address;
		leaf->size = 0;
		btff->root->node = leaf;
		stack[LEAF].node = leaf;
		stack[LEAF].available = 0;
	}
	else
		leaf = far_right_leaf(stack, ROOT, NULL);
	end = leaf->available + (int)leaf->size;
	if(end[-1] & AVAILABLE)
	{
		end = leaf_last(leaf, &left_available, &begin, &address, &available);
		leaf_update(leaf, begin, end, NULL, NULL);
		if(stack[LEAF].available == available)
		{
			stack[LEAF].available = left_available;
			available_decrease(stack, LEAF - 1);
		}
	}
	else
		address = btff->sbrk(0);
	if(size % alignment)
	{
		size /= alignment;
		size++;
		size *= alignment;
	}
	while((ALIGNMENT - 1) & size)
		size++;
	if((unsigned long)address % (unsigned long)alignment)
	{
		void* address_end;
		{
			unsigned long value = (unsigned long)address;
			value /= alignment;
			value++;
			value *= alignment;
			address_end = (void*)value;
		}
		available = address_end - address;
		begin = tmp;
		end = leaf_append(begin, available);
		end[-1] |= AVAILABLE;
		if(LEAF_SIZE < leaf->size + (end - begin)) 
		{
			leaf_overflow(leaf);
			leaf = far_right_leaf(stack, overflow(stack, LEAF), node_split);
		}
		if(-1 == btff->brk(address_end))
			GOTO_ERROR;
		leaf_update(leaf, leaf->available + (int)leaf->size, leaf->available + (int)leaf->size, begin, end);
		if(stack[LEAF].available < available)
		{
			stack[LEAF].available = available;
			available_increase(stack, LEAF - 1);
		}
		address = address_end;
	}
	begin = tmp;
	end = leaf_append(begin, size);
	if(LEAF_SIZE < leaf->size + (end - begin))
	{
		leaf_overflow(leaf);
		leaf = far_right_leaf(stack, overflow(stack, LEAF), node_split);
	}
	if(-1 == btff->brk(address + size))
		GOTO_ERROR;
	leaf_update(leaf, leaf->available + (int)leaf->size, leaf->available + (int)leaf->size, begin, end);
	return address;
ERROR:
	btff_perror(__FUNCTION__);
	if(0 != errno)
		btff_perror(sys_errlist[errno]);
	return NULL;
}

static void* btff_malloc(struct stack* stack, size_t size)
{
	void* ptr;
	int level;
	struct node* node;
	int middle_level;
	struct leaf* leaf;
	unsigned char tmp[12];
	unsigned char* begin;
	unsigned char* end;
	void* address;
	unsigned long available;
	unsigned long old_available;
	unsigned long left_available;
	int i;
	void (*split)(struct stack*, int, int);
	split = NULL;
	if(size == 0)
		goto RETURN;
	while(size & (ALIGNMENT - 1))
		size++;
	if(stack[ROOT].available < size)
		return brk_memalign(stack, ALIGNMENT, size);
	level = ROOT;
NODE_SEARCH:
	if(LEAF > (level = node_search_available(stack, level, size, split, &i)))
	{
		node = stack[level].node;
		middle_level = level;
	}
	else
	if(LEAF == level)
	{
		leaf = stack[LEAF].node;
		GOTO_LEAF_SEARCH;
	}
	else
		GOTO_ERROR;
/* NODE_FOUND: */
	if(size < node->available[i])
	{
	DEBUG;
		available = node->available[i] - size;
		if(!(leaf = right_leaf(stack, middle_level, split, &i)))
			GOTO_ERROR;
	SPLIT:
		begin = tmp;
		end = leaf_append(begin, available);
		end[-1] |= AVAILABLE;
		if(LEAF_SIZE < leaf->size + (end - begin))
		{
		DEBUG;
			leaf_overflow(leaf);
			level = overflow(stack, LEAF);
			if(level < middle_level)
			{
			DEBUG;
				split = node_split;
				goto NODE_SEARCH;
			}
			else
			if(level == middle_level)
			{
			DEBUG;
				leaf = right_leaf(stack, level, node_split, &i);
				goto SPLIT;
			}
			else
			{
			DEBUG;
				leaf = far_left_leaf(stack, level, node_split);
				goto SPLIT;
			}
		}
		old_available = node->available[i];
		node->available[i] = size;
		if(stack[middle_level].available == old_available)
		{
			stack[middle_level].available = node_available(node->available, node->size);
			available_decrease(stack, middle_level - 1);
		}
		leaf->address -= available;
		leaf_update(leaf, leaf->available, leaf->available, begin, end);
		if(stack[LEAF].available < available)
		{
			stack[LEAF].available = available;
			available_increase(stack, LEAF - 1);
		}
	}
	/* if(node->available[i] == size) */
DEBUG;
	old_available = node->available[i];
	node->available[i] = 0;
	if(stack[middle_level].available == old_available)
	{
		stack[middle_level].available = node_available(node->available, node->size);
		available_decrease(stack, middle_level - 1);
	}
	ptr = node->address[i];
	goto RETURN;
LEAF_SEARCH:
	if((end = leaf_search_available(leaf, size, &left_available, &begin, &address, &available)))
	{		
		if(size == available)
		{
			end[-1] &= ~AVAILABLE;	
			if(stack[LEAF].available == available)
			{
				stack[LEAF].available = leaf_available(end, leaf->size - (end - leaf->available));
				if(stack[LEAF].available < left_available)
					stack[LEAF].available = left_available;
				available_decrease(stack, LEAF - 1);
			}
			ptr = address;
		}
		else
		if(size < available)
		{
			unsigned char* tmp_begin;
			unsigned char* tmp_middle;
			unsigned char* tmp_end;
			tmp_begin = tmp;
			tmp_middle = leaf_append(tmp_begin, size);
			tmp_end = leaf_append(tmp_middle, available - size);
			tmp_end[-1] |= AVAILABLE;
			if(leaf->size - (end - begin) + (tmp_end - tmp_begin) <= LEAF_SIZE)
			{
				end = leaf_update(leaf, begin, end, tmp_begin, tmp_end);
				tmp_middle = end - (tmp_end - tmp_middle);
				if(stack[LEAF].available == available)
				{
					stack[LEAF].available = leaf_available(tmp_middle, leaf->size - (tmp_middle - leaf->available));
					if(stack[LEAF].available < left_available)
						stack[LEAF].available = left_available;
					available_decrease(stack, LEAF - 1);
				}
				ptr = address;			
			/* BRK */
				tmp_end = leaf_next(tmp_middle, &available);
				if((tmp_end == (leaf->available + (int)leaf->size)))
				{
					if(btff->sbrk(0) == address + available)
					{
						if(!(tmp_end[1] & AVAILABLE))
							GOTO_ERROR;
						if(-1 == brk(address))
							GOTO_ERROR;
						leaf_update(leaf, tmp_middle, tmp_end, NULL, NULL);	
						if(stack[LEAF].available == available)
						{
							if(stack[LEAF].available < left_available)
								stack[LEAF].available = left_available;
							available_decrease(stack, LEAF - 1);
						}
					}
				}
			}
			else
			{
			DEBUG;
				leaf_overflow(leaf);
				level = overflow(stack, LEAF);
				split = node_split;
				goto NODE_SEARCH;
			}
		}
	}
	else
		GOTO_ERROR;
RETURN:
	return ptr;
ERROR:
	btff_perror(__FUNCTION__);
	if(0 != errno)
		btff_perror(sys_errlist[errno]);
	return NULL;
}

static void btff_free(struct stack* stack, void *ptr)
{
	int level;
	struct node* node;
	int middle_level;
	int r, m, l;
	struct leaf* leaf;
	void* address;
	unsigned char* left;
	unsigned char* middle;
	unsigned char* right;
	unsigned char* end;
	unsigned long available;
	void* ptr_end;
	unsigned decrease;
	int left_level;
	int right_level;
	unsigned long left_available;
	unsigned leaf_brk;
	level = ROOT;
/* COALESCE: */
	if(LEAF > (level = node_search_address(stack, level, ptr, NULL, &m)))
		middle_level = level;
	else
	if(LEAF == level)
	{
		leaf = stack[LEAF].node;
		GOTO_LEAF_SEARCH;
	}
	else
		GOTO_ERROR;
	node = stack[middle_level].node;
	if(0 < node->available[m])
		GOTO_ERROR;
/* COALESCE_RIGHT: */
DEBUG;
	if((leaf = right_leaf(stack, middle_level, NULL, &m)))
	{
		ptr_end = leaf->address;
		right = leaf->available;
		end = leaf_next(right, &available);
		if(end[-1] & AVAILABLE)
		{
		DEBUG;
			leaf->address = (ptr_end += available);

			leaf_update(leaf, right, end, NULL, NULL);
			if(stack[LEAF].available == available)
			{
			DEBUG;
				stack[LEAF].available = leaf_available(leaf->available, leaf->size);
				available_decrease(stack, LEAF - 1);
			}
			if(leaf->size <= LEAF_MIDDLE)
			{
			DEBUG;
				if((level = rebalance(stack, LEAF)) <= middle_level)
				{
					if(LEAF > (level = node_search_address(stack, level, ptr, NULL, &m)))
					{
					DEBUG;
						middle_level = level;
					}
					else
					if(LEAF == level)
					{
					DEBUG;
						leaf = stack[LEAF].node;
						GOTO_LEAF_SEARCH;
					}
					else
						GOTO_ERROR;
				}
			}
		}
	}
	else
		GOTO_ERROR;
	node = stack[middle_level].node;
	if(0 < node->available[m])
		GOTO_ERROR;
/* COALESCE_LEFT: */
DEBUG;
	if((leaf = left_leaf(middle_level, NULL, &m)))
	{
		end = leaf_last(leaf, &left_available, &left, &address, &available);
		if(end[-1] & AVAILABLE)
		{
		DEBUG;
			node->address[m] = ptr = address;

			leaf_update(leaf, left, end, NULL, NULL);
			if(stack[LEAF].available == available)
			{
			DEBUG;
				stack[LEAF].available = left_available;
				available_decrease(stack, LEAF - 1);
			}
			if(leaf->size <= LEAF_MIDDLE)
			{
			DEBUG;
				if((level = rebalance(stack, LEAF)) <= middle_level)
				{	
					if(LEAF > (level = node_search_address(stack, level, ptr, NULL, &m)))
					{
					DEBUG;
						middle_level = level;
					}
					else
					if(LEAF == level)
					{
					DEBUG;
						leaf = stack[LEAF].node;
						GOTO_LEAF_SEARCH;
					}
					else
						GOTO_ERROR;
				}
			}
		}
	}
	else
		GOTO_ERROR;
	node = stack[middle_level].node;
	if(0 < node->available[m])
		GOTO_ERROR;
	node->available[m] = ptr_end - ptr;
	if(stack[middle_level].available < node->available[m])
	{
		stack[middle_level].available = node->available[m];
		available_increase(stack, middle_level - 1);
	}
	goto RETURN;
LEAF_SEARCH:
	if(!(right = leaf_search_address(leaf, ptr, &left, &middle, &address, &available)))
		GOTO_ERROR;
	if(right[-1] & AVAILABLE)
		GOTO_ERROR;
/* LEAF_FOUND: */
	ptr_end = ptr + available;
	left_level = right_level = -1;
	decrease = 0;
	leaf_brk = 0;
	if(right < leaf->available + (int)leaf->size)
	{
		end = leaf_next(right, &available);
		if(end[-1] & AVAILABLE)
		{
			unsigned char tmp_begin[12];
			unsigned char* tmp_end;
		DEBUG;
			if(stack[LEAF].available == available)
				decrease = 1;
			ptr_end += available;
			tmp_end = leaf_append(tmp_begin, ptr_end - ptr);
			right = leaf_update(leaf, middle, end, tmp_begin, tmp_end);
		}
	}
	else
	{
		if(ROOT <= (right_level = right_node(stack, &r)))
		{
			node = stack[right_level].node;
			if(node->available[r] <= 0)
				right_level = -1;
		}
		else 
			leaf_brk = 1;
	}
	if(left)
	{
		middle = leaf_next(left, &available);
		if(middle[-1] & AVAILABLE)
		{
			unsigned char tmp_begin[12];
			unsigned char* tmp_end;
		DEBUG;
			if(stack[LEAF].available == available)
				decrease = 1;
			ptr -= available;
			tmp_end = leaf_append(tmp_begin, ptr_end - ptr);
			right = leaf_update(leaf, left, right, tmp_begin, tmp_end);
			middle = left;
			left = NULL;
		}
	}
	else
	{
		if(ROOT <= (left_level = left_node(stack, &l)))
		{
			node = stack[left_level].node;
			if(node->available[l] <= 0)
				left_level = -1;
		}
	}
	if(ROOT <= right_level)
	{
	DEBUG;
		leaf_update(leaf, middle, right, NULL, NULL);
		if(decrease)
		{
		DEBUG;
			stack[LEAF].available = leaf_available(leaf->available, leaf->size);
			available_decrease(stack, LEAF - 1);
		}
		node = stack[right_level].node;
		ptr_end = node->address[r] + node->available[r];
		node->address[r] = ptr;
		node->available[r] = ptr_end - ptr;
		if(stack[right_level].available < node->available[r])
		{
		DEBUG;
			stack[right_level].available = node->available[r];
			available_increase(stack, right_level - 1);
		}
	}
	else
	if(ROOT <= left_level)
	{
	DEBUG;
		right = leaf_next(middle, &available);
		leaf->address += available;
		leaf_update(leaf, middle, right, NULL, NULL);
		if(decrease)
		{
		DEBUG;
			stack[LEAF].available = leaf_available(leaf->available, leaf->size);
			available_decrease(stack, LEAF - 1);
		}
		node = stack[left_level].node;
		ptr = node->address[l];
		node->available[l] = ptr_end - ptr;
		if(stack[left_level].available < node->available[l])
		{
		DEBUG;
			stack[left_level].available = node->available[l];
			available_increase(stack, left_level - 1);
		}
	}
	else
	{
	DEBUG;
		right[-1] |= AVAILABLE;
		available = ptr_end - ptr;
		if(stack[LEAF].available < available)
		{
		DEBUG;
			stack[LEAF].available = available;
			available_increase(stack, LEAF - 1);
		}
	}
	if(leaf_brk)
	{
		right = leaf->available + leaf->size;
		if(right[-1] & AVAILABLE)
		{
			right = leaf_last(leaf, &left_available, &middle, &address, &available);
			if(address != ptr)
				GOTO_ERROR;
			if(address + available != ptr_end)
				GOTO_ERROR;
			if(-1 == btff->brk(ptr))
				GOTO_ERROR;
			leaf_update(leaf, middle, right, NULL, NULL);
			if(stack[LEAF].available == available)
			{
				stack[LEAF].available = left_available;
				available_decrease(stack, LEAF - 1);
			}
		}
	}
	if(leaf->size <= LEAF_MIDDLE)
		level = rebalance(stack, LEAF);
RETURN:
	return;
ERROR:
	btff_perror(__FUNCTION__);
	if(0 != errno)
		btff_perror(sys_errlist[errno]);
	exit(EXIT_FAILURE);
	return;
}

static void *btff_realloc(struct stack* stack, void *old, size_t* old_size, size_t new_size)
{
	void* old_end;
	void* new;
	int m, r;
	void (*split)(struct stack*, int, int);
	int level;
	struct node* node;
	struct leaf* leaf;
	unsigned char* middle, * right, * end;
	unsigned long available;
	int middle_level, right_level;
	int delta;
	unsigned char tmp_begin[12];
	unsigned char* tmp_middle;
	unsigned char* tmp_end;
	while(new_size & (ALIGNMENT - 1))
		new_size++;
	level = ROOT;
	split = NULL;
NODE_SEARCH:
	if(LEAF > (level = node_search_address(stack, level, old, split, &m)))
		middle_level = level;
	else
	if(LEAF == level)
	{
		leaf = stack[LEAF].node;
		GOTO_LEAF_SEARCH;
	}
	else
		GOTO_ERROR;
	node = stack[middle_level].node;
	if(0 < node->available[m])
		goto ERROR;
	old = node->address[m];
	leaf = right_leaf(stack, middle_level, split, &m);
	old_end = leaf->address;
	if(new_size < old_end - old)
	{
		unsigned char tmp_begin[12];
		unsigned char* tmp_end;
		delta = (old_end - old) - new_size;
		tmp_end = leaf_append(tmp_begin, delta);
		tmp_end[-1] |= AVAILABLE;
		if(LEAF_SIZE < leaf->size + (tmp_end - tmp_begin))
		{
			leaf_overflow(leaf);
			level = overflow(stack, LEAF);
			if(level < middle_level)
			{
				split = node_split;
				goto NODE_SEARCH;
			}
			else
			if(level == middle_level)
				leaf = right_leaf(stack, level, node_split, &m);
			else
				leaf = far_left_leaf(stack, level, node_split);
		}
		leaf->address -= delta;
		leaf_update(leaf, leaf->available, leaf->available, tmp_begin, tmp_end);
		if(stack[LEAF].available < delta)
		{
			stack[LEAF].available = delta;
			available_increase(stack, LEAF - 1);
		}
		return old;
	}
	else
	if(old_end - old < new_size)
	{
		unsigned char* begin;
		unsigned char* end;
		unsigned long available;
		delta = new_size - (old_end - old);
		begin = leaf->available;
		end = leaf_next(begin, &available);
		if((end[-1] & AVAILABLE) && (delta <= available))
		{
			if(delta < available)
			{
				unsigned char tmp_begin[12];
				unsigned char* tmp_end;
				tmp_end = leaf_append(tmp_begin, available - delta);
				tmp_end[-1] |= AVAILABLE;
				leaf_update(leaf, begin, end, tmp_begin, tmp_end);
			}
			else
				leaf_update(leaf, begin, end, NULL, NULL);
			leaf->address += delta;
			if(stack[LEAF].available == available)
			{
				stack[LEAF].available = leaf_available(leaf->available, leaf->size);
				available_decrease(stack, LEAF - 1);
			}
			goto OLD;
		}
		else
			goto NEW;
	}
	else
		return old;
LEAF_SEARCH:
	if(!(right = leaf_search_address(leaf, old, NULL, &middle, NULL, &available)))
		GOTO_ERROR;
	if(right[-1] & AVAILABLE)
		GOTO_ERROR;
	old_end = old + available;
	right_level = -1;
	tmp_end = tmp_middle = tmp_begin;
	if(new_size < old_end - old)
	{
		delta = (old_end - old) - new_size;
		if(right < leaf->available + (int)leaf->size)
		{
			end = leaf_next(right, &available);
			if(end[-1] & AVAILABLE)
			{
				tmp_middle = leaf_append(tmp_begin, new_size);
				tmp_end = leaf_append(tmp_middle, delta + available);
				delta += available;
				right = end;
			}
			else
			{
				tmp_middle = leaf_append(tmp_begin, new_size);
				tmp_end = leaf_append(tmp_middle, delta);
			}
			tmp_end[-1] |= AVAILABLE;
		}
		else
		if(ROOT <= (right_level = right_node(stack, &r)))
		{
			node = stack[right_level].node;
			if(node->available[r] <= 0)
			{
				tmp_middle = leaf_append(tmp_begin, new_size);
				tmp_end = leaf_append(tmp_middle, delta);
				tmp_end[-1] |= AVAILABLE;
			}
			else
			{
				tmp_end = leaf_append(tmp_begin, new_size);
				node->address[r] -= delta;
				node->available[r] += delta;
				if(stack[right_level].available < node->available[r])
				{
					stack[right_level].available = node->available[r];
					available_increase(stack, right_level - 1);
				}
				delta = 0;
			}
		}
		else /* BRK */
		{
			if(-1 == btff->brk(old + new_size))
				GOTO_ERROR;
			tmp_end = leaf_append(tmp_begin, new_size);
			delta = 0;
		}
		if(LEAF_SIZE < leaf->size - (right - middle) + tmp_end - tmp_begin)
		{
			leaf_overflow(leaf);
			level = overflow(stack, LEAF);
			split = node_split;
			goto NODE_SEARCH;
		}
		leaf_update(leaf, middle, right, tmp_begin, tmp_end);
		if(stack[LEAF].available < delta)
		{
			stack[LEAF].available = delta;
			available_increase(stack, LEAF - 1);
		}
	}
	else
	if(old_end - old < new_size)
	{
		unsigned decrease = 0;
		delta = new_size - (old_end - old);
		if(right < leaf->available + (int)leaf->size)
		{
			end = leaf_next(right, &available);
			if(end[-1] & AVAILABLE)
			{
				tmp_middle = leaf_append(tmp_begin, new_size);
				if(available < delta)
					goto NEW;
				else
				if(delta < available)
				{
					tmp_end = leaf_append(tmp_middle, available - delta);
					tmp_end[-1] |= AVAILABLE;
				}
				else
					tmp_end = tmp_middle;
				right = end;
				if(stack[LEAF].available == available)
					decrease = 1;
			}
			else
				goto NEW;
		}
		else
		if(ROOT <= (right_level = right_node(stack, &r)))
		{
			node = stack[right_level].node;
			if(node->available[r] < delta)
				goto NEW;
			else
			if(delta < node->available[r])
			{
				tmp_end = leaf_append(tmp_begin, new_size);	
				if(LEAF_SIZE < leaf->size - (right - middle) + (tmp_end - tmp_begin))
				{
					leaf_overflow(leaf);
					level = overflow(stack, LEAF);
					split = node_split;
					goto NODE_SEARCH;
				}
				node->address[r] += delta;
				available = node->available[r];
				node->available[r] -= delta;
				if(stack[right_level].available == available)
				{
					stack[right_level].available = node_available(node->available, node->size);
					available_decrease(stack, right_level - 1);
				}
			}
			else
			{
				node->address[r] -= old_end - old;
				available = node->available[r];
				node->available[r] = 0;
				if(stack[right_level].available == available)
				{
					stack[right_level].available = node_available(node->available, node->size);
					available_decrease(stack, right_level - 1);
				}
				leaf_update(leaf, middle, right, NULL, NULL);
				goto OLD;
			}
		}
		else /* BRK */
		{
			if(-1 == btff->brk(old + new_size))
				GOTO_ERROR;
			tmp_end = leaf_append(tmp_begin, new_size);
		}
		if(LEAF_SIZE < leaf->size - (right - middle) + (tmp_end - tmp_begin))
		{
			leaf_overflow(leaf);
			level = overflow(stack, LEAF);
			split = node_split;
			goto NODE_SEARCH;
		}
		leaf_update(leaf, middle, right, tmp_begin, tmp_end);
		if(decrease)
		{
			stack[LEAF].available = leaf_available(leaf->available, leaf->size);
			available_decrease(stack, LEAF - 1);
		}
	}
OLD:
	if(leaf->size <= LEAF_MIDDLE)
		rebalance(stack, LEAF);
	*old_size = new_size;
	return old;
NEW:		
	btff_free(stack, old);
	new = btff_malloc(stack, new_size);
	*old_size = old_end - old;
	return new;
ERROR:
	btff_perror(__FUNCTION__);
	if(0 != errno)
		btff_perror(sys_errlist[errno]);
	return NULL;
}

static void available_check(void* root, int level)
{
	struct node* node = root;
	int i;
	if(level + 1 < LEAF)
	{
		for(i = 0; i < node->size; i += 2)
		{
			struct node* child = node->address[i];
			if(node->available[i] != node_available(child->available, child->size))
			{
			DEBUG;
				printf("available check error\n");
				exit(EXIT_FAILURE);
			}
			available_check(node->address[i], level + 1);
		}
	}
	else
	if(level + 1 == LEAF)
	{
		for(i = 0; i < node->size; i += 2)
		{
			struct leaf* leaf = node->address[i];
			if(node->available[i] != leaf_available(leaf->available, leaf->size))
			{
			DEBUG;
				printf("available check error\n");
				exit(EXIT_FAILURE);
			}
		}
	}
}

static void sanity_check(void* p, int level, void* address_end)
{
	if(!p)
		return;
	if(level == LEAF)
	{
		struct leaf* leaf = p;
		if(address_end != leaf_address_end(leaf))
		{
		DEBUG;
			printf("sanity check error\n");
			printf("%08x %08x %08x\n", (unsigned int)leaf->address, (unsigned int)leaf_address_end(leaf), (unsigned int)address_end);
			exit(EXIT_FAILURE);
		}
	}
	else
	{
		int i;
		struct node* node = p;
		if(node->level != level)
		{
		DEBUG;
			printf("sanity check error\n");
			exit(EXIT_FAILURE);
		}
		if(0 == (node->size % 2))
		{
		DEBUG;
			printf("sanity check error\n");
			exit(EXIT_FAILURE);
		}
		for(i = 0; i < node->size - 1; i += 2)
			sanity_check(node->address[i], level + 1, node->address[i + 1]);
		sanity_check(node->address[i], level + 1, address_end);
	}
}

