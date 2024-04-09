#pragma once
#include <stdint.h>
#include <stdio.h>

typedef struct node {
    struct node *next;
    struct node *prev;
    void *data;
} node;

typedef struct circular_buffer
{
    node *head;
    node *tail;
} circular_buffer;

void cb_init(circular_buffer *cb, size_t capacity, size_t sz);

void cb_free(circular_buffer *cb);

int cb_push_back(circular_buffer *cb, const void *item);

int cb_pop_front(circular_buffer *cb);

int cb_is_full(circular_buffer *cb);

int cb_is_empty(circular_buffer *cb);

void *cb_tail(circular_buffer *cb);

void *cb_head(circular_buffer *cb);

// void *cb_get_next(circular_buffer *cb, void *cur);