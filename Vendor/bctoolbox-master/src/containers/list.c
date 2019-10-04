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

#define _CRT_RAND_S
#include <stddef.h>
#include <stdlib.h>
#include <sys/stat.h>

#ifndef _WIN32
#include <unistd.h>
#include <sys/time.h> /*for gettimeofday*/
#include <dirent.h> /* available on POSIX system only */
#else
#include <direct.h>
#endif
#include "bctoolbox/port.h"
#include "bctoolbox/logging.h"
#include "bctoolbox/list.h"


bctbx_list_t* bctbx_list_new(void *data){
	bctbx_list_t* new_elem=bctbx_new0(bctbx_list_t,1);
	new_elem->data=data;
	return new_elem;
}
bctbx_list_t* bctbx_list_next(const bctbx_list_t *elem) {
	return elem->next;
}
void* bctbx_list_get_data(const bctbx_list_t *elem) {
	return elem->data;
}
bctbx_list_t*  bctbx_list_append_link(bctbx_list_t* elem,bctbx_list_t *new_elem){
	bctbx_list_t* it=elem;
	if (elem==NULL)  return new_elem;
	if (new_elem==NULL)  return elem;
	while (it->next!=NULL) it=bctbx_list_next(it);
	it->next=new_elem;
	new_elem->prev=it;
	return elem;
}

bctbx_list_t*  bctbx_list_append(bctbx_list_t* elem, void * data){
	bctbx_list_t* new_elem=bctbx_list_new(data);
	return bctbx_list_append_link(elem,new_elem);
}

bctbx_list_t*  bctbx_list_prepend_link(bctbx_list_t* elem, bctbx_list_t *new_elem){
	if (elem!=NULL) {
		new_elem->next=elem;
		elem->prev=new_elem;
	}
	return new_elem;
}

bctbx_list_t*  bctbx_list_prepend(bctbx_list_t* elem, void *data){
	return bctbx_list_prepend_link(elem,bctbx_list_new(data));
}

bctbx_list_t * bctbx_list_last_elem(const bctbx_list_t *l){
	if (l==NULL) return NULL;
	while(l->next){
		l=l->next;
	}
	return (bctbx_list_t*)l;
}

bctbx_list_t * bctbx_list_first_elem(const bctbx_list_t *l){
	if (l==NULL) return NULL;
	while(l->prev){
		l=l->prev;
	}
	return (bctbx_list_t*)l;
}

bctbx_list_t*  bctbx_list_concat(bctbx_list_t* first, bctbx_list_t* second){
	bctbx_list_t* it=first;
	if (it==NULL) return second;
	if (second==NULL) return first;
	while(it->next!=NULL) it=bctbx_list_next(it);
	it->next=second;
	second->prev=it;
	return first;
}

bctbx_list_t*  bctbx_list_free(bctbx_list_t* list){
	bctbx_list_t* elem = list;
	bctbx_list_t* tmp;
	if (list==NULL) return NULL;
	while(elem->next!=NULL) {
		tmp = elem;
		elem = elem->next;
		bctbx_free(tmp);
	}
	bctbx_free(elem);
	return NULL;
}

bctbx_list_t * bctbx_list_free_with_data(bctbx_list_t *list, bctbx_list_free_func freefunc){
	bctbx_list_t* elem = list;
	bctbx_list_t* tmp;
	if (list==NULL) return NULL;
	while(elem->next!=NULL) {
		tmp = elem;
		elem = elem->next;
		freefunc(tmp->data);
		bctbx_free(tmp);
	}
	freefunc(elem->data);
	bctbx_free(elem);
	return NULL;
}


bctbx_list_t*  _bctbx_list_remove(bctbx_list_t* first, void *data, int warn_if_not_found){
	bctbx_list_t* it;
	it=bctbx_list_find(first,data);
	if (it) return bctbx_list_erase_link(first,it);
	else if (warn_if_not_found){
		bctbx_warning("bctbx_list_remove: no element with %p data was in the list", data);
	}
	return first;
}

bctbx_list_t*  bctbx_list_remove(bctbx_list_t* first, void *data){
	return _bctbx_list_remove(first, data, TRUE);
}

bctbx_list_t * bctbx_list_remove_custom(bctbx_list_t *first, bctbx_compare_func compare_func, const void *user_data) {
	bctbx_list_t *cur;
	bctbx_list_t *elem = first;
	while (elem != NULL) {
		cur = elem;
		elem = elem->next;
		if (compare_func(cur->data, user_data) == 0) {
			first = bctbx_list_remove(first, cur->data);
		}
	}
	return first;
}

size_t bctbx_list_size(const bctbx_list_t* first){
	size_t n=0;
	while(first!=NULL){
		++n;
		first=first->next;
	}
	return n;
}

void bctbx_list_for_each(const bctbx_list_t* list, bctbx_list_iterate_func func){
	for(;list!=NULL;list=list->next){
		func(list->data);
	}
}

void bctbx_list_for_each2(const bctbx_list_t* list, bctbx_list_iterate2_func func, void *user_data){
	for(;list!=NULL;list=list->next){
		func(list->data,user_data);
	}
}

bctbx_list_t * bctbx_list_pop_front(bctbx_list_t *list, void **front_data){
	bctbx_list_t *front_elem=list;
	if (front_elem==NULL){
		*front_data=NULL;
		return NULL;
	}
	*front_data=front_elem->data;
	list=bctbx_list_unlink(list,front_elem);
	bctbx_free(front_elem);
	return list;
}

bctbx_list_t* bctbx_list_unlink(bctbx_list_t* list, bctbx_list_t* elem){
	bctbx_list_t* ret;
	if (elem==list){
		ret=elem->next;
		elem->prev=NULL;
		elem->next=NULL;
		if (ret!=NULL) ret->prev=NULL;
		return ret;
	}
	elem->prev->next=elem->next;
	if (elem->next!=NULL) elem->next->prev=elem->prev;
	elem->next=NULL;
	elem->prev=NULL;
	return list;
}

bctbx_list_t * bctbx_list_erase_link(bctbx_list_t* list, bctbx_list_t* elem){
	bctbx_list_t *ret=bctbx_list_unlink(list,elem);
	bctbx_free(elem);
	return ret;
}

bctbx_list_t* bctbx_list_find(bctbx_list_t* list, const void *data){
	for(;list!=NULL;list=list->next){
		if (list->data==data) return list;
	}
	return NULL;
}

bctbx_list_t* bctbx_list_find_custom(const bctbx_list_t* list, bctbx_compare_func compare_func, const void *user_data){
	for(;list!=NULL;list=list->next){
		if (compare_func(list->data,user_data)==0) return (bctbx_list_t *)list;
	}
	return NULL;
}

bctbx_list_t *bctbx_list_delete_custom(bctbx_list_t *list, bctbx_compare_func compare_func, const void *user_data){
	bctbx_list_t *elem=bctbx_list_find_custom(list,compare_func,user_data);
	if (elem!=NULL){
		list=bctbx_list_erase_link(list,elem);
	}
	return list;
}

void * bctbx_list_nth_data(const bctbx_list_t* list, int index){
	int i;
	for(i=0;list!=NULL;list=list->next,++i){
		if (i==index) return list->data;
	}
	bctbx_error("bctbx_list_nth_data: no such index in list.");
	return NULL;
}

int bctbx_list_position(const bctbx_list_t* list, bctbx_list_t* elem){
	int i;
	for(i=0;list!=NULL;list=list->next,++i){
		if (elem==list) return i;
	}
	bctbx_error("bctbx_list_position: no such element in list.");
	return -1;
}

int bctbx_list_index(const bctbx_list_t* list, void *data){
	int i;
	for(i=0;list!=NULL;list=list->next,++i){
		if (data==list->data) return i;
	}
	bctbx_error("bctbx_list_index: no such element in list.");
	return -1;
}

bctbx_list_t* bctbx_list_insert_sorted(bctbx_list_t* list, void *data, bctbx_compare_func compare_func){
	bctbx_list_t* it,*previt=NULL;
	bctbx_list_t* nelem;
	bctbx_list_t* ret=list;
	if (list==NULL) return bctbx_list_append(list,data);
	else{
		nelem=bctbx_list_new(data);
		for(it=list;it!=NULL;it=it->next){
			previt=it;
			if (compare_func(data,it->data)<=0){
				nelem->prev=it->prev;
				nelem->next=it;
				if (it->prev!=NULL)
					it->prev->next=nelem;
				else{
					ret=nelem;
				}
				it->prev=nelem;
				return ret;
			}
		}
		previt->next=nelem;
		nelem->prev=previt;
	}
	return ret;
}

bctbx_list_t* bctbx_list_insert(bctbx_list_t* list, bctbx_list_t* before, void *data){
	bctbx_list_t* elem;
	if (list==NULL || before==NULL) return bctbx_list_append(list,data);
	for(elem=list;elem!=NULL;elem=bctbx_list_next(elem)){
		if (elem==before){
			if (elem->prev==NULL)
				return bctbx_list_prepend(list,data);
			else{
				bctbx_list_t* nelem=bctbx_list_new(data);
				nelem->prev=elem->prev;
				nelem->next=elem;
				elem->prev->next=nelem;
				elem->prev=nelem;
			}
		}
	}
	return list;
}

bctbx_list_t* bctbx_list_copy(const bctbx_list_t* list){
	bctbx_list_t* copy=NULL;
	const bctbx_list_t* iter;
	for(iter=list;iter!=NULL;iter=bctbx_list_next(iter)){
		copy=bctbx_list_append(copy,iter->data);
	}
	return copy;
}

bctbx_list_t* bctbx_list_copy_with_data(const bctbx_list_t* list, bctbx_list_copy_func copyfunc){
	bctbx_list_t* copy=NULL;
	const bctbx_list_t* iter;
	for(iter=list;iter!=NULL;iter=bctbx_list_next(iter)){
		copy=bctbx_list_append(copy,copyfunc(iter->data));
	}
	return copy;
}

bctbx_list_t* bctbx_list_copy_reverse_with_data(const bctbx_list_t* list, bctbx_list_copy_func copyfunc){
	bctbx_list_t* copy=NULL;
	const bctbx_list_t* iter;
	for(iter=list;iter!=NULL;iter=bctbx_list_next(iter)){
		copy=bctbx_list_prepend(copy,copyfunc(iter->data));
	}
	return copy;
}
