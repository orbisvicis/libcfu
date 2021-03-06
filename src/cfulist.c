/*
 * cfulist.c - This file is part of the libcfu library
 *
 * Copyright (c) 2005 Don Owens. All rights reserved.
 *
 * This code is released under the BSD license:
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *
 *   * Redistributions in binary form must reproduce the above
 *     copyright notice, this list of conditions and the following
 *     disclaimer in the documentation and/or other materials provided
 *     with the distribution.
 *
 *   * Neither the name of the author nor the names of its
 *     contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include "cfu.h"

#include <stdlib.h>
#include <string.h>
#include <assert.h>

#ifdef HAVE_PTHREAD_H
# include <pthread.h>
#endif

#include "cfulist.h"
#include "cfustring.h"

typedef struct cfulist_entry {
	void *data;
	size_t data_size;
	struct cfulist_entry *next;
	struct cfulist_entry *prev;
} cfulist_entry;

struct cfulist {
	libcfu_type type;
	cfulist_entry *entries;
	cfulist_entry *tail;
	size_t num_entries;
#ifdef HAVE_PTHREAD_H
	pthread_mutex_t mutex;
#endif
	cfulist_entry *each_ptr;
	cfulist_free_fn_t free_fn;
};

cfulist_t *
cfulist_new(void) {
	cfulist_t *list;
	if (!(list = malloc(sizeof(*list))))
		return list;
	*list = (cfulist_t){.type=libcfu_t_list};
#ifdef HAVE_PTHREAD_H
	pthread_mutex_init(&list->mutex, NULL);
#endif
	return list;
}

cfulist_t *
cfulist_new_with_free_fn(cfulist_free_fn_t free_fn) {
	cfulist_t *list = cfulist_new();
	list->free_fn = free_fn;
	return list;
}

static CFU_INLINE void
_cfulist_free_entry(cfulist_entry *entry, cfulist_free_fn_t list_ff,
		cfulist_free_fn_t override_ff, void **data, size_t *data_size) {
	cfulist_free_fn_t ff;
	if (!entry)
		return;
	ff = override_ff ? override_ff : list_ff;
	if (ff) {
		ff(entry->data);
		entry->data = NULL;
		entry->data_size = 0;
	}
	if (data)
		*data = entry->data;
	if (data_size)
		*data_size = entry->data_size;
	free(entry);
}

size_t
cfulist_num_entries(cfulist_t *list) {
	return list->num_entries;
}

static CFU_INLINE void
lock_list(cfulist_t *list) {
#ifdef HAVE_PTHREAD_H
	pthread_mutex_lock(&list->mutex);
#endif
}

static CFU_INLINE void
unlock_list(cfulist_t *list) {
#ifdef HAVE_PTHREAD_H
	pthread_mutex_unlock(&list->mutex);
#endif
}

static CFU_INLINE cfulist_entry *
new_list_entry(void) {
	cfulist_entry *entry;
	if (!(entry = malloc(sizeof(*entry))))
		return entry;
	*entry = (struct cfulist_entry){0};
	return entry;
}

int
cfulist_push_data(cfulist_t *list, void *data, size_t data_size) {
	cfulist_entry *entry = new_list_entry();
	if (!entry) return 0;

	if (data_size == (size_t)-1) data_size = strlen((char *)data) + 1;

	entry->data = data;
	entry->data_size = data_size;

	lock_list(list);

	if (list->tail) {
		entry->prev = list->tail;
		list->tail->next = entry;
		list->tail = entry;
	} else {
		list->tail = list->entries = entry;
	}
	list->num_entries++;

	unlock_list(list);

	return 1;
}

int
cfulist_pop_data(cfulist_t *list, void **data, size_t *data_size) {
	if (!list) {
		*data = NULL;
		*data_size = 0;
		return 0;
	}
	lock_list(list);

	if (list->tail) {
		if (list->tail->prev) {
			cfulist_entry *new_tail = list->tail->prev;
			assert(list->num_entries > 1);
			new_tail->next = NULL;
			*data = list->tail->data;
			if (data_size) *data_size = list->tail->data_size;
			free(list->tail);
			list->tail = new_tail;
		} else {
			/* there is only one entry in the list */
			assert(list->num_entries == 1);
			*data = list->tail->data;
			if (data_size) *data_size = list->tail->data_size;
			free(list->tail);
			list->tail = NULL;
			list->entries = NULL;
		}
		list->num_entries--;
		unlock_list(list);
		return 1;
	}

	unlock_list(list);

	if (data_size) *data_size = 0;
	return 0;
}

int
cfulist_unshift_data(cfulist_t *list, void *data, size_t data_size) {
	cfulist_entry *entry = new_list_entry();
	if (!entry) return 0;

	if (data_size == (size_t)-1) data_size = strlen((char *)data) + 1;

	entry->data = data;
	entry->data_size = data_size;

	lock_list(list);

	if (list->entries) {
		entry->next = list->entries;
		list->entries->prev = entry;
		list->entries = entry;
	} else {
		list->tail = list->entries = entry;
	}
	list->num_entries++;

	unlock_list(list);

	return 1;
}

int
cfulist_shift_data(cfulist_t *list, void **data, size_t *data_size) {
	int rv = 0;
	if (!list) {
		if (data_size) *data_size = 0;
		*data = NULL;
		return rv;
	}

	lock_list(list);

	if (list->entries) {
		cfulist_entry *entry = list->entries;
		assert(list->num_entries >= 1);
		rv = 1;
		*data = entry->data;
		if (data_size) *data_size = entry->data_size;
		if (entry->next) {
			assert(list->num_entries > 1);
			list->entries = entry->next;
			list->entries->prev = NULL;
		} else {
			assert(list->num_entries == 1);
			list->tail = NULL;
			list->entries = NULL;
		}
		free(entry);
		list->num_entries--;
	} else {
		assert(list->num_entries == 0);
		rv = 0;
		if (data_size) *data_size = 0;
		*data = NULL;
	}

	unlock_list(list);
	return rv;
}

int
cfulist_enqueue_data(cfulist_t *list, void *data, size_t data_size) {
	return cfulist_push_data(list, data, data_size);
}


int
cfulist_dequeue_data(cfulist_t *list, void **data, size_t *data_size) {
	return cfulist_shift_data(list, data, data_size);
}

int
cfulist_first_data(cfulist_t *list, void **data, size_t *data_size) {
	int rv = 0;
	if (!list) {
		return 0;
	}

	lock_list(list);
	if (list->entries) {
		rv = 1;
		*data = list->entries->data;
		if (data_size) *data_size = list->entries->data_size;
	} else {
		rv = 0;
		*data = NULL;
		*data_size = 0;
	}
	unlock_list(list);

	return rv;
}

int
cfulist_last_data(cfulist_t *list, void **data, size_t *data_size) {
	int rv = 0;
	if (!list) {
		return 0;
	}

	lock_list(list);
	if (list->tail) {
		rv = 1;
		*data = list->tail->data;
		if (data_size) *data_size = list->tail->data_size;
	} else {
		rv = 0;
		*data = NULL;
		*data_size = 0;
	}
	unlock_list(list);

	return rv;
}

// Add a new entry, shifting the old entry either right of left. If entry_old
// is NULL, a right shift places the new entry at the head of the list. If
// entry_old is NULL, a left shift places the new entry at the tail of the
// list.
int
_cfulist_add_entry_at(cfulist_t *list, cfulist_entry *entry_new,
		cfulist_entry *entry_old, int shift_old_left) {
	if (!list || !entry_new)
		return 0;
	if (shift_old_left) {
		if (!entry_old)
			entry_old = list->tail;

		entry_new->prev = entry_old;
		entry_new->next = entry_old ? entry_old->next : entry_old;

		if (entry_old && entry_old->next)
			entry_old->next->prev = entry_new;
		else
			list->tail = entry_new;

		if (entry_old)
			entry_old->next = entry_new;
	}
	else {
		if (!entry_old)
			entry_old = list->entries;

		entry_new->prev = entry_old? entry_old->prev : entry_old;
		entry_new->next = entry_old;

		if (entry_old && entry_old->prev)
			entry_old->prev->next = entry_new;
		else
			list->entries = entry_new;

		if (entry_old)
			entry_old->prev = entry_new;
	}
	list->num_entries++;
	assert(list->entries && !list->entries->prev);
	assert(list->tail && !list->tail->next);
	return 1;
}

int
_cfulist_unlink_entry(cfulist_t *list, cfulist_entry *entry) {
	if (!list || !entry)
		return 0;
	if (entry->next)
		entry->next->prev = entry->prev;
	else
		list->tail = entry->prev;
	if (entry->prev)
		entry->prev->next = entry->next;
	else
		list->entries = entry->next;
	assert(list->num_entries > 0);
	list->num_entries--;
	if (list->entries)
		assert(!list->entries->prev);
	if (list->tail)
		assert(!list->tail->next);
	return 1;
}

int
_cfulist_remove_entry(cfulist_t *list, cfulist_entry *entry,
		cfulist_free_fn_t ff, void **data, size_t *data_size) {
	if (!list || !entry)
		return 0;
	if (!_cfulist_unlink_entry(list, entry))
		return 0;
	_cfulist_free_entry(entry, list->free_fn, ff, data, data_size);
	return 1;
}

cfulist_entry *
_cfulist_find_entry(cfulist_t *list, size_t n) {
	size_t i;
	cfulist_entry *entry;
	
	if (!list || n >= list->num_entries)
		return NULL;
	
	for (i = 0, entry = list->entries; entry && i < n; i++, entry = entry->next)
		;

	if (entry)
		assert(i == n);

	return entry;
}

cfulist_entry *
_cfulist_find_entry_relative(cfulist_t *list, int n) {
	if (!list)
		return NULL;

	if (n < 0)
		n += list->num_entries;

	if (n < 0)
		return NULL;

	return _cfulist_find_entry(list, n);
}

int
cfulist_nth_data(cfulist_t *list, void **data, size_t *data_size, size_t n) {
	cfulist_entry *entry;
	int status;

	if (!list)
		return 0;

	lock_list(list);
	if (entry = _cfulist_find_entry(list, n)) {
		if (data)
			*data = entry->data;
		if (data_size)
			*data_size = entry->data_size;
		status = 1;
	}
	else {
		status = 0;
	}
	unlock_list(list);

	return status;
}

int
cfulist_remove_nth_data(cfulist_t *list, void **data, size_t *data_size,
		size_t n, cfulist_free_fn_t ff) {
	cfulist_entry *entry;
	int status;
	
	if (!list)
		return 0;

	lock_list(list);
	entry = _cfulist_find_entry(list, n);
	status = _cfulist_remove_entry(list, entry, ff, data, data_size);
	unlock_list(list);
	return status;
}

void
cfulist_reset_each(cfulist_t *list) {
	if (!list) return;
	list->each_ptr = list->entries;
}

int
cfulist_each_data(cfulist_t *list, void **data, size_t *data_size) {
	if (!list)
		return 0;
	cfulist_reset_each(list);
	return cfulist_next_data(list, data, data_size);
}

int
cfulist_next_data(cfulist_t *list, void **data, size_t *data_size) {
	if (!list)
		return 0;
	if (list->each_ptr) {
		if (data)
			*data = list->each_ptr->data;
		if (data_size)
			*data_size = list->each_ptr->data_size;
		list->each_ptr = list->each_ptr->next;
		return 1;
	}
	return 0;
}

size_t
cfulist_foreach_remove(cfulist_t *list, cfulist_remove_fn_t r_fn,
		cfulist_free_fn_t ff, void *arg) {
	cfulist_entry *entry = NULL;
	size_t num_removed = 0;

	if (!list)
		return num_removed;
	
	lock_list(list);
	for (entry = list->entries; entry; entry = entry->next) {
		if (!r_fn(entry->data, entry->data_size, arg))
			continue;
		if (!_cfulist_remove_entry(list, entry, ff, NULL, NULL))
			continue;
		num_removed++;
	}
	unlock_list(list);

	return num_removed;
}

size_t
cfulist_foreach(cfulist_t *list, cfulist_foreach_fn_t fe_fn, void *arg) {
	cfulist_entry *entry = NULL;
	size_t num_processed = 0;
	int rv = 0;

	if (!list) return 0;

	lock_list(list);
	for (entry = list->entries; entry && !rv; entry = entry->next) {
		rv = fe_fn(entry->data, entry->data_size, arg);
		num_processed++;
	}

	unlock_list(list);

	return num_processed;
}

typedef struct _cfulist_map_struct {
	cfulist_t *list;
	void *arg;
	cfulist_map_fn_t map_fn;
} _cfulist_map_ds;

static int
_cfulist_map_foreach(void *data, size_t data_size, void *arg) {
	_cfulist_map_ds *ds = (_cfulist_map_ds *)arg;
	size_t new_data_size = 0;
	void *rv = ds->map_fn(data, data_size, ds->arg, &new_data_size);
	cfulist_push_data(ds->list, rv, new_data_size);
	return 0;
}

cfulist_t *
cfulist_map(cfulist_t *list, cfulist_map_fn_t map_fn, void *arg) {
	cfulist_t *new_list = cfulist_new();
	_cfulist_map_ds ds;
	ds.list = new_list;
	ds.arg = arg;
	ds.map_fn = map_fn;

	cfulist_foreach(list, _cfulist_map_foreach, (void *)&ds);
	return new_list;
}

/* For when you don't care about the data size */

int
cfulist_push(cfulist_t *list, void *data) {
	return cfulist_push_data(list, data, 0);
}

void *
cfulist_pop(cfulist_t *list) {
	void *data = NULL;
	if (cfulist_pop_data(list, &data, NULL)) {
		return data;
	}
	return NULL;
}

int
cfulist_unshift(cfulist_t *list, void *data) {
	return cfulist_unshift_data(list, data, 0);
}

void *
cfulist_shift(cfulist_t *list) {
	void *data = NULL;
	if (cfulist_shift_data(list, &data, NULL)) {
		return data;
	}
	return NULL;
}

int
cfulist_enqueue(cfulist_t *list, void *data) {
	return cfulist_push(list, data);
}

void *
cfulist_dequeue(cfulist_t *list) {
	return cfulist_shift(list);
}

/* Dealing with strings */

int
cfulist_push_string(cfulist_t *list, char *data) {
	return cfulist_push_data(list, (void *)data, -1);
}

char *
cfulist_pop_string(cfulist_t *list) {
	void *data = NULL;
	if (cfulist_pop_data(list, &data, NULL)) {
		return (char *)data;
	}
	return NULL;
}

int
cfulist_unshift_string(cfulist_t *list, char *data) {
	return cfulist_unshift_data(list, (void *)data, -1);
}

char *
cfulist_shift_string(cfulist_t *list) {
	void *data = NULL;
	if (cfulist_shift_data(list, &data, NULL)) {
		return (char *)data;
	}
	return NULL;
}

int
cfulist_enqueue_string(cfulist_t *list, char *data) {
	return cfulist_push_string(list, data);
}

char *
cfulist_dequeue_string(cfulist_t *list) {
	return cfulist_shift_string(list);
}

typedef struct _join_foreach_struct {
	size_t count;
	const char *delimiter;
	cfustring_t *string;
} _join_foreach_struct;

static int
_join_foreach_fn(void *data, size_t data_size, void *arg) {
	_join_foreach_struct *stuff = (_join_foreach_struct *)arg;

	data_size = data_size;
	if (stuff->count) cfustring_append(stuff->string, stuff->delimiter);
	stuff->count++;
	cfustring_append(stuff->string, (char *)data);
	return 0;
}

char *
cfulist_join(cfulist_t *list, const char *delimiter) {
	_join_foreach_struct *arg = calloc(1, sizeof(_join_foreach_struct));
	char *str = NULL;

	arg->delimiter = delimiter;
	arg->string = cfustring_new();
	cfulist_foreach(list, _join_foreach_fn, (void *)arg);

	str = cfustring_get_buffer_copy(arg->string);
	cfustring_destroy(arg->string);
	free(arg);

	return str;
}

void
cfulist_destroy(cfulist_t *list) {
	cfulist_destroy_with_free_fn(list, NULL);
}

void
cfulist_destroy_with_free_fn(cfulist_t *list, cfulist_free_fn_t free_fn) {
	cfulist_entry *entry;
	cfulist_entry *next;

	if (!list)
		return;

	lock_list(list);
	entry = list->entries;
	while (entry) {
		next = entry->next;
		_cfulist_free_entry(entry, list->free_fn, free_fn, NULL, NULL);
		entry = next;
	}
	unlock_list(list);
#ifdef HAVE_PTHREAD_H
	pthread_mutex_destroy(&list->mutex);
#endif
	free(list);
}
