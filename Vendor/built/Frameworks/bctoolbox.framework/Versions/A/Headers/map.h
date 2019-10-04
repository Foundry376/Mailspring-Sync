/*
    bctoolbox mmap
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

#ifndef BCTBX_MMAP_H_
#define BCTBX_MMAP_H_
#include "bctoolbox/list.h"
#include "bctoolbox/port.h"

#ifdef __cplusplus
extern "C"{
#endif

typedef struct _bctbx_map_t bctbx_map_t;
typedef struct _bctbx_pair_t bctbx_pair_t;
typedef struct _bctbx_iterator_t bctbx_iterator_t;

typedef struct _bctbx_mmap_ullong_t bctbx_mmap_ullong_t;
typedef struct _bctbx_mmap_cchar_t bctbx_mmap_cchar_t;
	
typedef void (*bctbx_map_free_func)(void *);
/*map*/
BCTBX_PUBLIC bctbx_map_t *bctbx_mmap_ullong_new(void);
BCTBX_PUBLIC bctbx_map_t *bctbx_mmap_cchar_new(void);
BCTBX_PUBLIC void bctbx_mmap_ullong_delete(bctbx_map_t *mmap);
BCTBX_PUBLIC void bctbx_mmap_cchar_delete(bctbx_map_t *mmap);
BCTBX_PUBLIC void bctbx_mmap_ullong_delete_with_data(bctbx_map_t *mmap, bctbx_map_free_func freefunc);
BCTBX_PUBLIC void bctbx_mmap_cchar_delete_with_data(bctbx_map_t *mmap, bctbx_map_free_func freefunc);
#define bctbx_map_insert bctbx_map_ullong_insert
BCTBX_PUBLIC void bctbx_map_ullong_insert(bctbx_map_t *map,const bctbx_pair_t *pair);
BCTBX_PUBLIC void bctbx_map_cchar_insert(bctbx_map_t *map,const bctbx_pair_t *pair);
/*same as insert, but also deleting pair*/
#define bctbx_map_insert_and_delete bctbx_map_ullong_insert_and_delete
BCTBX_PUBLIC void bctbx_map_ullong_insert_and_delete(bctbx_map_t *map,bctbx_pair_t *pair);
BCTBX_PUBLIC void bctbx_map_cchar_insert_and_delete(bctbx_map_t *map,bctbx_pair_t *pair);
/*same as insert and deleting pair with a newly allocated it returned */
#define bctbx_map_insert_and_delete_with_returned_it bctbx_map_ullong_insert_and_delete_with_returned_it
BCTBX_PUBLIC bctbx_iterator_t * bctbx_map_ullong_insert_and_delete_with_returned_it(bctbx_map_t *map,bctbx_pair_t *pair);
BCTBX_PUBLIC bctbx_iterator_t * bctbx_map_cchar_insert_and_delete_with_returned_it(bctbx_map_t *map,bctbx_pair_t *pair);
/*at return, it point to the next element*/
#define bctbx_map_erase bctbx_map_ullong_erase
BCTBX_PUBLIC bctbx_iterator_t *bctbx_map_ullong_erase(bctbx_map_t *map,bctbx_iterator_t *it);
BCTBX_PUBLIC bctbx_iterator_t *bctbx_map_cchar_erase(bctbx_map_t *map,bctbx_iterator_t *it);
/*return a new allocated iterator*/
#define bctbx_map_begin bctbx_map_ullong_begin
BCTBX_PUBLIC bctbx_iterator_t *bctbx_map_ullong_begin(const bctbx_map_t *map);
BCTBX_PUBLIC bctbx_iterator_t *bctbx_map_cchar_begin(const bctbx_map_t *map);
/*return a new allocated iterator*/
#define bctbx_map_end bctbx_map_ullong_end
BCTBX_PUBLIC bctbx_iterator_t *bctbx_map_ullong_end(const bctbx_map_t *map);
BCTBX_PUBLIC bctbx_iterator_t *bctbx_map_cchar_end(const bctbx_map_t *map);
/*return a new allocated iterator or null*/
#define bctbx_map_find_custom bctbx_map_ullong_find_custom
BCTBX_PUBLIC bctbx_iterator_t * bctbx_map_ullong_find_custom(bctbx_map_t *map, bctbx_compare_func compare_func, const void *user_data);
BCTBX_PUBLIC bctbx_iterator_t * bctbx_map_cchar_find_custom(bctbx_map_t *map, bctbx_compare_func compare_func, const void *user_data);
/*return the iterator associated to the key in the map or Null*/
BCTBX_PUBLIC bctbx_iterator_t * bctbx_map_ullong_find_key(bctbx_map_t *map, unsigned long long key);
	BCTBX_PUBLIC bctbx_iterator_t * bctbx_map_cchar_find_key(bctbx_map_t *map, const char * key);
/* return the size of the map*/
#define bctbx_map_size bctbx_map_ullong_size
BCTBX_PUBLIC size_t bctbx_map_ullong_size(const bctbx_map_t *map);
BCTBX_PUBLIC size_t bctbx_map_cchar_size(const bctbx_map_t *map);

/*iterator*/
#define bctbx_iterator_get_pair bctbx_iterator_ullong_get_pair
BCTBX_PUBLIC bctbx_pair_t *bctbx_iterator_ullong_get_pair(const bctbx_iterator_t *it);
BCTBX_PUBLIC bctbx_pair_t *bctbx_iterator_cchar_get_pair(const bctbx_iterator_t *it);
/*return same pointer but pointing to next*/
#define bctbx_iterator_get_next bctbx_iterator_ullong_get_next
BCTBX_PUBLIC bctbx_iterator_t *bctbx_iterator_ullong_get_next(bctbx_iterator_t *it);
BCTBX_PUBLIC bctbx_iterator_t *bctbx_iterator_cchar_get_next(bctbx_iterator_t *it);
#define bctbx_iterator_equals bctbx_iterator_ullong_equals
BCTBX_PUBLIC  bool_t bctbx_iterator_ullong_equals(const bctbx_iterator_t *a,const bctbx_iterator_t *b);
BCTBX_PUBLIC  bool_t bctbx_iterator_cchar_equals(const bctbx_iterator_t *a,const bctbx_iterator_t *b);
#define bctbx_iterator_delete bctbx_iterator_ullong_delete
BCTBX_PUBLIC void bctbx_iterator_ullong_delete(bctbx_iterator_t *it);
BCTBX_PUBLIC void bctbx_iterator_cchar_delete(bctbx_iterator_t *it);
	
/*pair*/	
typedef struct _bctbx_pair_ullong_t bctbx_pair_ullong_t; /*inherit from bctbx_pair_t*/
BCTBX_PUBLIC bctbx_pair_ullong_t * bctbx_pair_ullong_new(unsigned long long key,void *value);
typedef struct _bctbx_pair_cchar_t bctbx_pair_cchar_t; /*inherit from bctbx_pair_t*/
BCTBX_PUBLIC bctbx_pair_cchar_t * bctbx_pair_cchar_new(const char * key,void *value);

#define bctbx_pair_get_second bctbx_pair_ullong_get_second
BCTBX_PUBLIC void* bctbx_pair_ullong_get_second(const bctbx_pair_t * pair);
BCTBX_PUBLIC void* bctbx_pair_cchar_get_second(const bctbx_pair_t * pair);
BCTBX_PUBLIC unsigned long long bctbx_pair_ullong_get_first(const bctbx_pair_ullong_t * pair);
BCTBX_PUBLIC const char * bctbx_pair_cchar_get_first(const bctbx_pair_cchar_t * pair);
#define bctbx_pair_delete bctbx_pair_ullong_delete
BCTBX_PUBLIC void bctbx_pair_ullong_delete(bctbx_pair_t * pair);
BCTBX_PUBLIC void bctbx_pair_cchar_delete(bctbx_pair_t * pair);

#ifdef __cplusplus
}
#endif

#endif /* BCTBX_LIST_H_ */
