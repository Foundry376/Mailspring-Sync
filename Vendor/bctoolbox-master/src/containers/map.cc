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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "bctoolbox/logging.h"
#include "bctoolbox/map.h"
#include <map>
#include <typeinfo> 

#define LOG_DOMAIN "bctoolbox"

typedef std::multimap<unsigned long long, void*> mmap_ullong_t;
typedef mmap_ullong_t::value_type pair_ullong_t;

typedef std::multimap<std::string, void*> mmap_cchar_t;
typedef mmap_cchar_t::value_type pair_cchar_t;

template<typename T> bctbx_map_t * bctbx_mmap_new(void) {
	return (bctbx_map_t *) new T;
}
extern "C" bctbx_map_t *bctbx_mmap_ullong_new(void) {
	return bctbx_mmap_new<mmap_ullong_t>();
}
extern "C" bctbx_map_t *bctbx_mmap_cchar_new(void) {
	return bctbx_mmap_new<mmap_cchar_t>();
}

template<typename T> void bctbx_mmap_delete(bctbx_map_t *mmap) {
	delete (T *)mmap;
}
extern "C" void bctbx_mmap_ullong_delete(bctbx_map_t *mmap) {
	bctbx_mmap_delete<mmap_ullong_t>(mmap);
}
extern "C" void bctbx_mmap_cchar_delete(bctbx_map_t *mmap) {
	bctbx_mmap_delete<mmap_cchar_t>(mmap);
}
extern "C" void bctbx_mmap_ullong_delete_with_data(bctbx_map_t *mmap, bctbx_map_free_func freefunc) {
	bctbx_iterator_t *it = bctbx_map_ullong_begin(mmap);
	bctbx_iterator_t *end = bctbx_map_ullong_end(mmap);
	
	while(!bctbx_iterator_ullong_equals(it, end)) {
		bctbx_pair_t *pair = bctbx_iterator_ullong_get_pair(it);
		freefunc(bctbx_pair_ullong_get_second(pair));
		it = bctbx_iterator_ullong_get_next(it);
	}
	bctbx_iterator_ullong_delete(it);
	bctbx_iterator_ullong_delete(end);
	bctbx_mmap_ullong_delete(mmap);
}
extern "C" void bctbx_mmap_cchar_delete_with_data(bctbx_map_t *mmap, bctbx_map_free_func freefunc) {
	bctbx_iterator_t *it = bctbx_map_cchar_begin(mmap);
	bctbx_iterator_t *end = bctbx_map_cchar_end(mmap);
	
	while(!bctbx_iterator_cchar_equals(it, end)) {
		bctbx_pair_t *pair = bctbx_iterator_cchar_get_pair(it);
		freefunc(bctbx_pair_cchar_get_second(pair));
		it = bctbx_iterator_cchar_get_next(it);
	}
	bctbx_iterator_cchar_delete(it);
	bctbx_iterator_cchar_delete(end);
	bctbx_mmap_cchar_delete(mmap);
}

template<typename T> bctbx_iterator_t *bctbx_map_insert_base(bctbx_map_t *map,const bctbx_pair_t *pair,bool_t returns_it) {
	typename T::iterator it;
	it = ((T *)map)->insert(*((typename T::value_type *)pair));
	if (returns_it) {
		return (bctbx_iterator_t *) new typename T::iterator(it);
	} else
		return NULL;
}
static bctbx_iterator_t * bctbx_map_ullong_insert_base(bctbx_map_t *map,const bctbx_pair_t *pair,bool_t returns_it) {
		return bctbx_map_insert_base<mmap_ullong_t>(map, pair, returns_it);
}
extern "C" void bctbx_map_ullong_insert(bctbx_map_t *map,const bctbx_pair_t *pair) {
	bctbx_map_ullong_insert_base(map,pair,FALSE);
}
static bctbx_iterator_t * bctbx_map_cchar_insert_base(bctbx_map_t *map,const bctbx_pair_t *pair,bool_t returns_it) {
	return bctbx_map_insert_base<mmap_cchar_t>(map, pair, returns_it);
}
extern "C" void bctbx_map_cchar_insert(bctbx_map_t *map,const bctbx_pair_t *pair) {
	bctbx_map_cchar_insert_base(map,pair,FALSE);
}
extern "C" void bctbx_map_ullong_insert_and_delete(bctbx_map_t *map, bctbx_pair_t *pair) {
	bctbx_map_ullong_insert(map,pair);
	bctbx_pair_ullong_delete(pair);
}
extern "C" bctbx_iterator_t * bctbx_map_ullong_insert_and_delete_with_returned_it(bctbx_map_t *map, bctbx_pair_t *pair) {
	bctbx_iterator_t * it = bctbx_map_ullong_insert_base(map,pair,TRUE);
	bctbx_pair_ullong_delete(pair);
	return it;
}
extern "C" void bctbx_map_cchar_insert_and_delete(bctbx_map_t *map, bctbx_pair_t *pair) {
	bctbx_map_cchar_insert(map,pair);
	bctbx_pair_cchar_delete(pair);
}
extern "C" bctbx_iterator_t * bctbx_map_cchar_insert_and_delete_with_returned_it(bctbx_map_t *map, bctbx_pair_t *pair) {
	bctbx_iterator_t * it = bctbx_map_cchar_insert_base(map,pair,TRUE);
	bctbx_pair_cchar_delete(pair);
	return it;
}

template<typename T> bctbx_iterator_t *bctbx_map_erase_type(bctbx_map_t *map,bctbx_iterator_t *it) {
	//bctbx_iterator_t *  next = (bctbx_iterator_t *) new mmap_ullong_t::iterator((*(mmap_ullong_t::iterator*)it));
	//next = bctbx_iterator_get_next(next);
	((T *)map)->erase((*(typename T::iterator*)it)++);
	//bctbx_iterator_delete(it);
	return it;
}
extern "C" bctbx_iterator_t *bctbx_map_ullong_erase(bctbx_map_t *map,bctbx_iterator_t *it) {
	return bctbx_map_erase_type<mmap_ullong_t>(map, it);
}
extern "C" bctbx_iterator_t *bctbx_map_cchar_erase(bctbx_map_t *map,bctbx_iterator_t *it) {
	return bctbx_map_erase_type<mmap_cchar_t>(map, it);
}

template<typename T> bctbx_iterator_t *bctbx_map_begin_type(const bctbx_map_t *map) {
	return (bctbx_iterator_t *) new typename T::iterator(((T *)map)->begin());
	
}
extern "C" bctbx_iterator_t *bctbx_map_ullong_begin(const bctbx_map_t *map) {
	return bctbx_map_begin_type<mmap_ullong_t>(map);
}
extern "C" bctbx_iterator_t *bctbx_map_cchar_begin(const bctbx_map_t *map) {
	return bctbx_map_begin_type<mmap_cchar_t>(map);
}

template<typename T> bctbx_iterator_t *bctbx_map_end_type(const bctbx_map_t *map) {
	return (bctbx_iterator_t *) new typename T::iterator(((T *)map)->end());
	
}
extern "C" bctbx_iterator_t *bctbx_map_ullong_end(const bctbx_map_t *map) {
	return bctbx_map_end_type<mmap_ullong_t>(map);
}
extern "C" bctbx_iterator_t *bctbx_map_cchar_end(const bctbx_map_t *map) {
	return bctbx_map_end_type<mmap_cchar_t>(map);
}

template<typename T> bctbx_iterator_t * bctbx_map_find_key_type(bctbx_map_t *map, typename T::key_type key) {
	bctbx_iterator_t * it = (bctbx_iterator_t*) new typename T::iterator(((T *)map)->find(key));
	return it;
}
extern "C" bctbx_iterator_t * bctbx_map_ullong_find_key(bctbx_map_t *map, unsigned long long key) {
	return bctbx_map_find_key_type<mmap_ullong_t>(map, (mmap_ullong_t::key_type)key);
}
extern "C" bctbx_iterator_t * bctbx_map_cchar_find_key(bctbx_map_t *map, const char * key) {
	return bctbx_map_find_key_type<mmap_cchar_t>(map, (mmap_cchar_t::key_type)key);
}

template<typename T> size_t bctbx_map_size_type(const bctbx_map_t *map) {
	return ((T *)map)->size();
}
extern "C" size_t bctbx_map_ullong_size(const bctbx_map_t *map) {
	return bctbx_map_size_type<mmap_ullong_t>(map);
}
extern "C" size_t bctbx_map_cchar_size(const bctbx_map_t *map) {
	return bctbx_map_size_type<mmap_cchar_t>(map);
}

extern "C" bctbx_iterator_t * bctbx_map_ullong_find_custom(bctbx_map_t *map, bctbx_compare_func compare_func, const void *user_data) {
	bctbx_iterator_t * end = bctbx_map_ullong_end(map);
	
	for(bctbx_iterator_t * it = bctbx_map_ullong_begin(map);!bctbx_iterator_ullong_equals(it,end);) {
		if (compare_func(bctbx_pair_ullong_get_second(bctbx_iterator_ullong_get_pair(it)),user_data)==0) {
			bctbx_iterator_ullong_delete(end);
			return it;
		} else {
			it = bctbx_iterator_ullong_get_next(it);
		}
		
	}
	bctbx_iterator_ullong_delete(end);
	return NULL;
}
extern "C" bctbx_iterator_t * bctbx_map_cchar_find_custom(bctbx_map_t *map, bctbx_compare_func compare_func, const void *user_data) {
	bctbx_iterator_t * end = bctbx_map_cchar_end(map);
	
	for(bctbx_iterator_t * it = bctbx_map_cchar_begin(map);!bctbx_iterator_cchar_equals(it,end);) {
		if (compare_func(bctbx_pair_cchar_get_second(bctbx_iterator_cchar_get_pair(it)),user_data)==0) {
			bctbx_iterator_cchar_delete(end);
			return it;
		} else {
			it = bctbx_iterator_cchar_get_next(it);
		}
		
	}
	bctbx_iterator_cchar_delete(end);
	return NULL;
}

/*iterator*/
template<typename T> bctbx_pair_t *bctbx_iterator_get_pair_type(const bctbx_iterator_t *it) {
	return (bctbx_pair_t *)&(**((typename T::iterator*)it));
}
extern "C" bctbx_pair_t *bctbx_iterator_ullong_get_pair(const bctbx_iterator_t *it) {
	return bctbx_iterator_get_pair_type<mmap_ullong_t>(it);
}
extern "C" bctbx_pair_t *bctbx_iterator_cchar_get_pair(const bctbx_iterator_t *it) {
	return bctbx_iterator_get_pair_type<mmap_cchar_t>(it);
}

template<typename T> bctbx_iterator_t *bctbx_iterator_get_next_type(bctbx_iterator_t *it) {
	((typename T::iterator*)it)->operator++();
	return it;
}
extern "C" bctbx_iterator_t *bctbx_iterator_ullong_get_next(bctbx_iterator_t *it) {
	return bctbx_iterator_get_next_type<mmap_ullong_t>(it);
}
extern "C" bctbx_iterator_t *bctbx_iterator_cchar_get_next(bctbx_iterator_t *it) {
	return bctbx_iterator_get_next_type<mmap_cchar_t>(it);
}
extern "C" bctbx_iterator_t *bctbx_iterator_ullong_get_next_and_delete(bctbx_iterator_t *it) {
	bctbx_iterator_t * next = bctbx_iterator_ullong_get_next(it);
	bctbx_iterator_ullong_delete(it);
	return next;
}
extern "C" bctbx_iterator_t *bctbx_iterator_cchar_get_next_and_delete(bctbx_iterator_t *it) {
	bctbx_iterator_t * next = bctbx_iterator_cchar_get_next(it);
	bctbx_iterator_cchar_delete(it);
	return next;
}

template<typename T> bool_t bctbx_iterator_equals_type(const bctbx_iterator_t *a,const bctbx_iterator_t *b) {
	return *(typename T::iterator*)a == *(typename T::iterator*)b;
}
extern "C" bool_t bctbx_iterator_ullong_equals(const bctbx_iterator_t *a,const bctbx_iterator_t *b) {
	return bctbx_iterator_equals_type<mmap_ullong_t>(a, b);
}
extern "C" bool_t bctbx_iterator_cchar_equals(const bctbx_iterator_t *a,const bctbx_iterator_t *b) {
	return bctbx_iterator_equals_type<mmap_cchar_t>(a, b);
}

template<typename T> void bctbx_iterator_delete_type(bctbx_iterator_t *it) {
	delete ((typename T::iterator*)it);
}
extern "C" void bctbx_iterator_ullong_delete(bctbx_iterator_t *it) {
	bctbx_iterator_delete_type<mmap_ullong_t>(it);
}
extern "C" void bctbx_iterator_cchar_delete(bctbx_iterator_t *it) {
	bctbx_iterator_delete_type<mmap_cchar_t>(it);
}

/*pair*/
template<typename T> typename T::value_type * bctbx_pair_new(typename T::key_type key,void *value) {
	return (typename T::value_type *) new typename T::value_type(key,value);
}
extern "C" bctbx_pair_ullong_t * bctbx_pair_ullong_new(unsigned long long key,void *value) {
	return (bctbx_pair_ullong_t *)bctbx_pair_new<mmap_ullong_t>((mmap_ullong_t::key_type)key, value);
}
extern "C" bctbx_pair_cchar_t * bctbx_pair_cchar_new(const char * key,void *value) {
	return (bctbx_pair_cchar_t *)bctbx_pair_new<mmap_cchar_t>((mmap_cchar_t::key_type)key, value);
}

template<typename T> const typename T::key_type& bctbx_pair_get_first(const typename T::value_type  * pair) {
	return ((typename T::value_type*)pair)->first;
}
extern "C" unsigned long long bctbx_pair_ullong_get_first(const bctbx_pair_ullong_t * pair) {
	return bctbx_pair_get_first<mmap_ullong_t>((pair_ullong_t *)pair);
}
extern "C" const char * bctbx_pair_cchar_get_first(const bctbx_pair_cchar_t * pair) {
	return bctbx_pair_get_first<mmap_cchar_t>((pair_cchar_t *)pair).c_str();
}

template<typename T> void* bctbx_pair_get_second_type(const bctbx_pair_t * pair) {
	return ((T*)pair)->second;
}
extern "C" void* bctbx_pair_ullong_get_second(const bctbx_pair_t * pair) {
	return bctbx_pair_get_second_type<pair_ullong_t>(pair);
}
extern "C" void* bctbx_pair_cchar_get_second(const bctbx_pair_t * pair) {
	return bctbx_pair_get_second_type<pair_cchar_t>(pair);
}

template<typename T> void bctbx_pair_delete_type(bctbx_pair_t * pair) {
	delete ((T*)pair);
}
extern "C" void bctbx_pair_ullong_delete(bctbx_pair_t * pair) {
	bctbx_pair_delete_type<pair_ullong_t>(pair);
}
extern "C" void bctbx_pair_cchar_delete(bctbx_pair_t * pair) {
	bctbx_pair_delete_type<pair_cchar_t>(pair);
}
