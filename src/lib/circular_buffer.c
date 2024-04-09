#include "circular_buffer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void cb_init(circular_buffer *cb, size_t capacity, size_t sz)
{
    node *head = malloc(sizeof(node));
    node *tail = malloc(sizeof(node));
    head->next = tail;
    head->prev = NULL;
    tail->next = NULL;
    tail->prev = head;
    cb->head = head;
    cb->tail = tail;
}

void cb_free(circular_buffer *cb)
{
    free(cb->head);
    free(cb->tail);
    free(cb);
}

int cb_push_back(circular_buffer *cb, const void *item)
{
    node *new_node = malloc(sizeof(node));
    new_node->data = item;
    new_node->next = cb->tail;
    new_node->prev = cb->tail->prev;
    cb->tail->prev = new_node;
    new_node->prev->next = new_node;

    return 1;
}

int cb_pop_front(circular_buffer *cb)
{
    if (cb->head->next == cb->tail) {
        return 0;
    }
    node *trash = cb->head->next;
    cb->head->next = trash->next;
    trash->next->prev = cb->head;
    free(trash);
    return 1;
}

int cb_is_full(circular_buffer *cb) {
    return 1;
}

int cb_is_empty(circular_buffer *cb) {
    return cb->head->next == cb->tail;
}

void *cb_tail(circular_buffer *cb) {
    return cb->tail;
}

void *cb_head(circular_buffer *cb) {
    return cb->head->next;
}

// void *cb_get_next(circular_buffer *cb, void *cur) {
//     cur = cur + cb->sz;
//     if (cur == cb->buffer_end) {
//         cur = cb->buffer;
//     }
//     return cur;
// }