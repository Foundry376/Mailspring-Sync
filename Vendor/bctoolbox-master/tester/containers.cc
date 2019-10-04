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

#include <stdio.h>
#include "bctoolbox_tester.h"
#include "bctoolbox/map.h"
#include "bctoolbox/list.h"

static void multimap_insert(void) {
	bctbx_map_t *mmap = bctbx_mmap_ullong_new();
	bctbx_list_t *head = NULL, *ref = NULL;
	bctbx_iterator_t *it,*end;
	long i=0;
	int N = 100;
	
	for(i=0;i<N;i++) {
		bctbx_pair_t* pair = (bctbx_pair_t*)bctbx_pair_ullong_new(N-i-1, (void*)((long)N-i-1));
		ref = bctbx_list_append(ref, (void*)i);
		bctbx_map_ullong_insert(mmap, pair);
		bctbx_pair_ullong_delete(pair);
	}
	BC_ASSERT_EQUAL(bctbx_map_ullong_size(mmap),N, int, "%i");
	
	it = bctbx_map_ullong_begin(mmap);
	head = bctbx_list_first_elem(ref);
	for(ref = bctbx_list_first_elem(ref);ref!=NULL;ref=bctbx_list_next(ref)) {
		BC_ASSERT_EQUAL( (long)bctbx_pair_ullong_get_second(bctbx_iterator_ullong_get_pair(it))
						, (long)bctbx_list_get_data(ref)
						, long, "%lu");
		it = bctbx_iterator_ullong_get_next(it);
	}
	bctbx_iterator_ullong_delete(it);
	
	ref = head;
	end = bctbx_map_ullong_end(mmap);
	
	for(it = bctbx_map_ullong_begin(mmap);!bctbx_iterator_ullong_equals(it,end);it = bctbx_iterator_ullong_get_next(it)) {
		BC_ASSERT_PTR_NOT_NULL(ref);
		BC_ASSERT_EQUAL( (long)bctbx_pair_ullong_get_second(bctbx_iterator_ullong_get_pair(it))
						, (long)bctbx_list_get_data(ref)
						, long, "%lu");
		ref=bctbx_list_next(ref);
	}
	bctbx_iterator_ullong_delete(it);
	bctbx_iterator_ullong_delete(end);
	bctbx_list_free(head);
	bctbx_mmap_ullong_delete(mmap);
}

static void multimap_erase(void) {
	bctbx_map_t *mmap = bctbx_mmap_ullong_new();
	bctbx_iterator_t *it;
	bctbx_iterator_t *end;
	long i=0;
	int N = 100;
	
	for(i=0;i<N;i++) {
		bctbx_map_ullong_insert_and_delete(mmap, (bctbx_pair_t*)bctbx_pair_ullong_new(i, (void*)((long)i)));
	}
	
	end= bctbx_map_ullong_end(mmap);
	
	for(it = bctbx_map_ullong_begin(mmap);!bctbx_iterator_ullong_equals(it,end);) {
		long value = (long)bctbx_pair_ullong_get_second(bctbx_iterator_ullong_get_pair(it));
		if (value < N/2) {
			it = bctbx_map_ullong_erase(mmap, it);
		} else {
			break;
		}
	}
	it = bctbx_map_ullong_begin(mmap);
	BC_ASSERT_EQUAL((long)bctbx_pair_ullong_get_second(bctbx_iterator_ullong_get_pair(it))
					, N/2
					,long, "%lu");
					
	bctbx_mmap_ullong_delete(mmap);
	bctbx_iterator_ullong_delete(end);
	
}
static int compare_func(const void *a, const void*b) {
	return (long)a == (long)b;
}
static void multimap_find_custom(void) {
	bctbx_map_t *mmap = bctbx_mmap_ullong_new();
	long i=0;
	int N = 100;
	
	for(i=0;i<N;i++) {
		bctbx_pair_t* pair = (bctbx_pair_t*)bctbx_pair_ullong_new(i, (void*)((long)i));
		bctbx_map_ullong_insert_and_delete(mmap, pair);
	}
	bctbx_iterator_t * it = bctbx_map_ullong_find_custom(mmap, compare_func, (void*)10l);
	BC_ASSERT_EQUAL((long)bctbx_pair_ullong_get_second(bctbx_iterator_ullong_get_pair(it))
					, 0
					,long, "%lu");
	bctbx_mmap_ullong_delete(mmap);
	bctbx_iterator_ullong_delete(it);
}

static void multimap_insert_cchar(void) {
	bctbx_map_t *mmap = bctbx_mmap_cchar_new();	bctbx_iterator_t *it,*end;
	long i=0;
	int N = 100;
	
	for(i=0;i<N;i++) {
		char str[32];
		snprintf(str, sizeof(str), "%ld", N-i-1);
		bctbx_pair_t* pair = (bctbx_pair_t*)bctbx_pair_cchar_new((const char *)str, (void*)((long)N-i-1));
		bctbx_map_cchar_insert(mmap, pair);
		bctbx_pair_cchar_delete(pair);
	}
	BC_ASSERT_EQUAL(bctbx_map_cchar_size(mmap),N, int, "%i");

	for(i = 0;i < N;i++) {
		char str[32];
		snprintf(str, sizeof(str), "%ld", i);
		it = bctbx_map_cchar_find_key(mmap, (const char *)str);
		BC_ASSERT_EQUAL( (long)bctbx_pair_cchar_get_second(bctbx_iterator_cchar_get_pair(it))
						, (long)i
						, long, "%lu");
	}
	
	end = bctbx_map_cchar_end(mmap);
	
	bctbx_iterator_cchar_delete(it);
	bctbx_iterator_cchar_delete(end);
	bctbx_mmap_cchar_delete(mmap);
}

static void multimap_erase_cchar(void) {
	bctbx_map_t *mmap = bctbx_mmap_cchar_new();
	bctbx_iterator_t *it;
	bctbx_iterator_t *end;
	long i=0;
	int N = 100;
	
	for(i=0;i<N;i++) {
		char str[32];
		snprintf(str, sizeof(str),"%ld", i);
		bctbx_map_cchar_insert_and_delete(mmap, (bctbx_pair_t*)bctbx_pair_cchar_new((const char *)str, (void*)((long)i)));
	}
	
	end= bctbx_map_cchar_end(mmap);
	
	for(it = bctbx_map_cchar_begin(mmap);!bctbx_iterator_cchar_equals(it,end);) {
		long value = (long)bctbx_pair_cchar_get_second(bctbx_iterator_cchar_get_pair(it));
		if (value < N/2) {
			it = bctbx_map_cchar_erase(mmap, it);
		} else {
			break;
		}
	}
	it = bctbx_map_cchar_begin(mmap);
	BC_ASSERT_EQUAL((long)bctbx_pair_cchar_get_second(bctbx_iterator_cchar_get_pair(it))
					, N/2
					,long, "%lu");
	
	bctbx_mmap_cchar_delete(mmap);
	bctbx_iterator_cchar_delete(end);
	
}
static int compare_func_cchar(const void *a, const void*b) {
	return (long)a == (long)b;
}
static void multimap_find_custom_cchar(void) {
	bctbx_map_t *mmap = bctbx_mmap_cchar_new();
	long i=0;
	int N = 100;
	
	for(i=0;i<N;i++) {
		char str[32];
		snprintf(str, sizeof(str), "%ld", i);
		bctbx_pair_t* pair = (bctbx_pair_t*)bctbx_pair_cchar_new((const char *)str, (void*)((long)i));
		bctbx_map_cchar_insert_and_delete(mmap, pair);
	}
	bctbx_iterator_t * it = bctbx_map_cchar_find_custom(mmap, compare_func, (void*)10l);
	BC_ASSERT_EQUAL((long)bctbx_pair_cchar_get_second(bctbx_iterator_cchar_get_pair(it))
					, 0
					,long, "%lu");
	bctbx_mmap_cchar_delete(mmap);
	bctbx_iterator_cchar_delete(it);
}


static test_t container_tests[] = {
	TEST_NO_TAG("mmap insert", multimap_insert),
	TEST_NO_TAG("mmap erase", multimap_erase),
	TEST_NO_TAG("mmap find custom", multimap_find_custom),
	TEST_NO_TAG("mmap insert cchar", multimap_insert_cchar),
	TEST_NO_TAG("mmap erase cchar", multimap_erase_cchar),
	TEST_NO_TAG("mmap find custom cchar", multimap_find_custom_cchar),
};

test_suite_t containers_test_suite = {"Containers", NULL, NULL, NULL, NULL,
							   sizeof(container_tests) / sizeof(container_tests[0]), container_tests};
