#include "hashtable.h"
#include "utils.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

linked_list_t *
ll_create(unsigned int data_size)
{
    linked_list_t* ll;

    ll = malloc(sizeof(*ll));
    DIE(!ll, "linked_list malloc");

    ll->head = NULL;
    ll->data_size = data_size;
    ll->size = 0;

    return ll;
}

void
ll_add_nth_node(linked_list_t* list, unsigned int n, const void* new_data)
{
    ll_node_t *prev, *curr;
    ll_node_t* new_node;

    if (!list) {
        return;
    }

    if (n > list->size) {
        n = list->size;
    }

    curr = list->head;
    prev = NULL;
    while (n > 0) {
        prev = curr;
        curr = curr->next;
        --n;
    }

    new_node = malloc(sizeof(*new_node));
    DIE(new_node == NULL, "new_node malloc");
    new_node->data = malloc(list->data_size);
    DIE(new_node->data == NULL, "new_node->data malloc");
    memcpy(new_node->data, new_data, list->data_size);

    new_node->next = curr;
    if (prev == NULL) {
        list->head = new_node;
    } else {
        prev->next = new_node;
    }

    list->size++;
}

ll_node_t *
ll_remove_nth_node(linked_list_t* list, unsigned int n)
{
    ll_node_t *prev, *curr;

    if (!list || !list->head) {
        return NULL;
    }

    if (n > list->size - 1) {
        n = list->size - 1;
    }

    curr = list->head;
    prev = NULL;
    while (n > 0) {
        prev = curr;
        curr = curr->next;
        --n;
    }

    if (prev == NULL) {
        /* Adica n == 0. */
        list->head = curr->next;
    } else {
        prev->next = curr->next;
    }

    list->size--;

    return curr;
}

unsigned int
ll_get_size(linked_list_t* list)
{
     if (!list) {
        return -1;
    }

    return list->size;
}

void
ll_free(linked_list_t** pp_list)
{
    ll_node_t* currNode;

    if (!pp_list || !*pp_list) {
        return;
    }

    while (ll_get_size(*pp_list) > 0) {
        currNode = ll_remove_nth_node(*pp_list, 0);
        free(currNode->data);
        currNode->data = NULL;
        free(currNode);
        currNode = NULL;
    }

    free(*pp_list);
    *pp_list = NULL;
}

void
ll_print_string(linked_list_t* list)
{
    ll_node_t* curr;

    if (!list) {
        return;
    }

    curr = list->head;
    while (curr != NULL) {
        printf("%s ", (char*)curr->data);
        curr = curr->next;
    }

    printf("\n");
}

int
compare_function_strings(void *a, void *b)
{
	char *str_a = (char *)a;
	char *str_b = (char *)b;

	return strcmp(str_a, str_b);
}

unsigned int
hash_function_string(void *a)
{
	/*
	 * Credits: http://www.cse.yorku.ca/~oz/hash.html
	 */
	unsigned char *puchar_a = (unsigned char*) a;
	unsigned int hash = 5381;
	int c;

	while ((c = *puchar_a++))
		hash = ((hash << 5u) + hash) + c; /* hash * 33 + c */

	return hash;
}

hashtable_t *
ht_create(unsigned int hmax, unsigned int (*hash_function)(void*),
		int (*compare_function)(void*, void*))
{
	hashtable_t *hashtable = malloc(sizeof(hashtable_t));

	DIE(!hashtable, "malloc failed!\n");

	hashtable->buckets = malloc(hmax * sizeof(linked_list_t *));

	DIE(!hashtable->buckets, "malloc failed!\n");

	hashtable->size = 0;
	hashtable->hmax = hmax;
	hashtable->hash_function = hash_function;
	hashtable->compare_function = compare_function;

	for (unsigned int i = 0; i < hmax; i++) {
		hashtable->buckets[i] = ll_create(sizeof(info));
	}

	return hashtable;
}

int
ht_has_key(hashtable_t *ht, void *key)
{
	DIE(!ht, "Create a hashtable first!\n");

	unsigned int index = ht->hash_function(key) % ht->hmax;

	ll_node_t *current = ht->buckets[index]->head;

	for (unsigned int i = 0; i < ht->buckets[index]->size; i++) {
		if (ht->compare_function(key, ((info *)current->data)->key) == 0)
			return 1;
		current = current->next;
	}

	return 0;
}

void *
ht_get(hashtable_t *ht, void *key)
{
	DIE(!ht, "Create a hashtable first!\n");

	unsigned int index = ht->hash_function(key) % ht->hmax;

	ll_node_t *current = ht->buckets[index]->head;

	for (unsigned int i = 0; i < ht->buckets[index]->size; i++) {
		if (ht->compare_function(key, ((info *)current->data)->key) == 0)
			return ((info *)current->data)->value;
		current = current->next;
	}

	return NULL;
}

hashtable_t *
ht_resize(hashtable_t *ht)
{
    hashtable_t *new_ht = ht_create(2 * ht->hmax, ht->hash_function,
        ht->compare_function);

    info *current_node;
    for (unsigned int i = 0; i < ht->hmax; i++) {
        ll_node_t *current = ht->buckets[i]->head;
        for (unsigned int k = 0; k < ht->buckets[i]->size; k++) {
            current_node = (info *)current->data;
            ht_put(&new_ht, current_node->key, current_node->key_size,
                current_node->value, current_node->value_size);
            current = current->next;
        }
    }

    return new_ht;
}

void
ht_put(hashtable_t **ht, void *key, unsigned int key_size,
	void *value, unsigned int value_size)
{
	DIE(!(*ht), "Create a hashtable first!\n");

    if ((*ht)->size == (*ht)->hmax) {
        hashtable_t *new_ht = ht_resize(*ht);
        ht_free(*ht);
        *ht = new_ht;
    }

	int index = (*ht)->hash_function(key) % (*ht)->hmax;

    if (ht_has_key(*ht, key) == 1) {
        ll_node_t* node = (*ht)->buckets[index]->head;
        while (node != NULL) {
            info* data_info = node->data;

            if (!(*ht)->compare_function(data_info->key, key)) {
                free(data_info->value);

                data_info->value_size = value_size;
                data_info->value = malloc(value_size);

                memcpy(data_info->value, value, value_size);
                return;
            }

            node = node->next;
        }
    }

	info new_data;
	new_data.key = malloc(key_size);
    new_data.key_size = key_size;
	new_data.value = malloc(value_size);
    new_data.value_size = value_size;

	memcpy(new_data.key, key, key_size);
	memcpy(new_data.value, value, value_size);

	ll_add_nth_node((*ht)->buckets[index], 0, &new_data);

	(*ht)->size++;
}

void
ht_remove_entry(hashtable_t *ht, void *key, void (*free_data)(void*))
{
	DIE(!ht, "Create a hashtable first!\n");

	int index = ht->hash_function(key) % ht->hmax;

	ll_node_t* current = ht->buckets[index]->head;

	int remove_index = -1;

	for (unsigned int i = 0; i < ht->buckets[index]->size; i++) {
		if (ht->compare_function(key, ((info *)current->data)->key) == 0) {
			remove_index = i;
			break;
		}
		current = current->next;
	}

	if (remove_index == -1)
		return;

	current = ll_remove_nth_node(ht->buckets[index], remove_index);

	free(((info *)current->data)->key);
	free_data(((info *)current->data)->value);
	free(current->data);
	free(current);

	ht->size--;
}

void
ht_free(hashtable_t *ht)
{
    DIE(!ht, "Create a hashtable first!\n");

    for (unsigned int i = 0; i < ht->hmax; i++) {
        ll_node_t *current = ht->buckets[i]->head;
        for (unsigned int k = 0; k < ht->buckets[i]->size; k++) {
            free(((info *)current->data)->key);
            free(((info *)current->data)->value);
            current = current->next;
        }
        ll_free(&ht->buckets[i]);
    }

    free(ht->buckets);

    free(ht);
}

unsigned int
ht_get_size(hashtable_t *ht)
{
	if (ht == NULL)
		return 0;

	return ht->size;
}

unsigned int
ht_get_hmax(hashtable_t *ht)
{
	if (ht == NULL)
		return 0;

	return ht->hmax;
}