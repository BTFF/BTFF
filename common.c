#include <malloc.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>

void *calloc(size_t nmemb, size_t size)
{
	unsigned long* ptr;
	if((ptr = malloc(nmemb * size)))
		return memset(ptr, 0, nmemb * size);
	return NULL;
}

int mallopt(int param, int value)
{
	return 0;
}

int malloc_trim (size_t pad) 
{ 
	return 1; 
}

size_t malloc_usable_size (void *ptr) 
{ 
	return 0; 
}

void malloc_stats (void) 
{ 
}

void *malloc_get_state (void) 
{ 
	return NULL; 
}

int malloc_set_state (void *ptr) 
{ 
	return -1; 
}

struct mallinfo mallinfo (void)
{
    struct mallinfo info;
    memset(&info, 0, sizeof(info));
    return info;
}

void *aligned_alloc(size_t alignment, size_t size)
{
	return memalign(alignment, size);
}   

void *valloc(size_t size)
{
	return memalign(sysconf(_SC_PAGESIZE),size);
}   

void *memalign(size_t alignment, size_t size)
{
	void* ptr;
	switch(posix_memalign(&ptr, alignment, size))
	{
	case EINVAL:
	case ENOMEM:
		return NULL;
	}
	return ptr;
}

void *pvalloc(size_t size)
{
	int page_size = sysconf(_SC_PAGESIZE);
	size /= page_size;
	size++;
	size *= page_size;
	return memalign(page_size, size);
}

