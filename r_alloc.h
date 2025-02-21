//Include guard
#ifndef __R_ALLOC_H__
#define __R_ALLOC_H__

//Standard library includes
#include <stdbool.h>
#include <stdlib.h>

__BEGIN_DECLS

//User facing functions
void*	r_malloc(size_t size);
void*	r_realloc(void *ptr, size_t size);
void	r_free(void *ptr);
size_t	r_alloc_size(void *ptr);
bool	r_allocated(void *ptr);
size_t	r_total_allocated(void);

__END_DECLS

#endif