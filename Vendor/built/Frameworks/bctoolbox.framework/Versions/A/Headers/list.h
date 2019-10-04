/*
    bctoolbox
    Copyright (C) 2016  Belledonne Communications SARL

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#ifndef BCTBX_LIST_H_
#define BCTBX_LIST_H_

#include "bctoolbox/port.h"

#ifdef __cplusplus
extern "C"{
#endif

typedef struct _bctbx_list {
	struct _bctbx_list *next;
	struct _bctbx_list *prev;
	void *data;
} bctbx_list_t;

typedef int (*bctbx_compare_func)(const void *, const void*);
typedef void (*bctbx_list_iterate_func)(void *);
typedef void (*bctbx_list_iterate2_func)(void *, void *);
typedef void (*bctbx_list_free_func)(void *);
typedef void* (*bctbx_list_copy_func)(void *);

BCTBX_PUBLIC bctbx_list_t * bctbx_list_new(void *data);
BCTBX_PUBLIC bctbx_list_t * bctbx_list_append(bctbx_list_t * elem, void * data);
BCTBX_PUBLIC bctbx_list_t * bctbx_list_append_link(bctbx_list_t * elem, bctbx_list_t *new_elem);
BCTBX_PUBLIC bctbx_list_t * bctbx_list_prepend(bctbx_list_t * elem, void * data);
BCTBX_PUBLIC bctbx_list_t * bctbx_list_prepend_link(bctbx_list_t* elem, bctbx_list_t *new_elem);
BCTBX_PUBLIC bctbx_list_t * bctbx_list_last_elem(const bctbx_list_t *l);
BCTBX_PUBLIC bctbx_list_t * bctbx_list_first_elem(const bctbx_list_t *l);
BCTBX_PUBLIC bctbx_list_t * bctbx_list_free(bctbx_list_t * elem);
BCTBX_PUBLIC bctbx_list_t * bctbx_list_concat(bctbx_list_t * first, bctbx_list_t * second);
BCTBX_PUBLIC bctbx_list_t * bctbx_list_remove(bctbx_list_t * first, void *data);
BCTBX_PUBLIC bctbx_list_t * bctbx_list_remove_custom(bctbx_list_t *first, bctbx_compare_func compare_func, const void *user_data);
BCTBX_PUBLIC bctbx_list_t * bctbx_list_pop_front(bctbx_list_t *list, void **front_data);
BCTBX_PUBLIC size_t bctbx_list_size(const bctbx_list_t * first);
BCTBX_PUBLIC void bctbx_list_for_each(const bctbx_list_t * list, bctbx_list_iterate_func func);
BCTBX_PUBLIC void bctbx_list_for_each2(const bctbx_list_t * list, bctbx_list_iterate2_func func, void *user_data);
/**
 * Removes the element pointed by elem from the list. The element itself is not freed, allowing 
 * to be chained in another list for example.
 * Use bctbx_list_erase_link() if you simply want to delete an element of a list.
**/
BCTBX_PUBLIC bctbx_list_t * bctbx_list_unlink(bctbx_list_t * list, bctbx_list_t * elem);
/**
 * Delete the element pointed by 'elem' from the list.
**/
BCTBX_PUBLIC bctbx_list_t * bctbx_list_erase_link(bctbx_list_t * list, bctbx_list_t * elem);
BCTBX_PUBLIC bctbx_list_t * bctbx_list_find(bctbx_list_t *list, const void *data);
BCTBX_PUBLIC bctbx_list_t * bctbx_list_free(bctbx_list_t *list);
/*frees list elements and associated data, using the supplied function pointer*/
BCTBX_PUBLIC bctbx_list_t * bctbx_list_free_with_data(bctbx_list_t *list, bctbx_list_free_func freefunc);

BCTBX_PUBLIC bctbx_list_t * bctbx_list_find_custom(const bctbx_list_t * list, bctbx_compare_func cmp, const void *user_data);
BCTBX_PUBLIC void * bctbx_list_nth_data(const bctbx_list_t * list, int index);
BCTBX_PUBLIC int bctbx_list_position(const bctbx_list_t * list, bctbx_list_t * elem);
BCTBX_PUBLIC int bctbx_list_index(const bctbx_list_t * list, void *data);
BCTBX_PUBLIC bctbx_list_t * bctbx_list_insert_sorted(bctbx_list_t * list, void *data, bctbx_compare_func cmp);
BCTBX_PUBLIC bctbx_list_t * bctbx_list_insert(bctbx_list_t * list, bctbx_list_t * before, void *data);
BCTBX_PUBLIC bctbx_list_t * bctbx_list_copy(const bctbx_list_t * list);
/*copy list elements and associated data, using the supplied function pointer*/
BCTBX_PUBLIC bctbx_list_t* bctbx_list_copy_with_data(const bctbx_list_t* list, bctbx_list_copy_func copyfunc);
/*Same as bctbx_list_copy_with_data but in reverse order*/
BCTBX_PUBLIC bctbx_list_t* bctbx_list_copy_reverse_with_data(const bctbx_list_t* list, bctbx_list_copy_func copyfunc);

BCTBX_PUBLIC bctbx_list_t* bctbx_list_next(const bctbx_list_t *elem);
BCTBX_PUBLIC void* bctbx_list_get_data(const bctbx_list_t *elem);
	
#ifdef __cplusplus
}
#endif

#endif /* BCTLBX_LIST_H_ */
