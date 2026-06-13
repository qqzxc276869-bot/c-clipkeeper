#ifndef CLIP_STORE_H
#define CLIP_STORE_H

#include <stddef.h>
#include <time.h>
#include <wchar.h>

typedef struct {
    unsigned long id;
    time_t created_at;
    int pinned;
    unsigned int copy_count;
    wchar_t *text;
} ClipItem;

typedef struct {
    ClipItem *items;
    size_t count;
    size_t capacity;
    unsigned long next_id;
} ClipStore;

typedef struct {
    unsigned long *ids;
    size_t count;
    size_t capacity;
} ClipSearchResult;

void store_init(ClipStore *store);
void store_free(ClipStore *store);

int store_load(ClipStore *store, const wchar_t *path);
int store_save(const ClipStore *store, const wchar_t *path);

ClipItem *store_add_text(ClipStore *store, const wchar_t *text);
ClipItem *store_find(ClipStore *store, unsigned long id);
const ClipItem *store_find_const(const ClipStore *store, unsigned long id);

int store_delete(ClipStore *store, unsigned long id);
int store_toggle_pin(ClipStore *store, unsigned long id);
void store_clear(ClipStore *store);
void store_increment_copy_count(ClipStore *store, unsigned long id);

void search_result_init(ClipSearchResult *result);
void search_result_free(ClipSearchResult *result);
int store_search(const ClipStore *store, const wchar_t *keyword, ClipSearchResult *result);

const wchar_t *store_preview_text(const wchar_t *text, wchar_t *buffer, size_t buffer_count);
void store_format_time(time_t value, wchar_t *buffer, size_t buffer_count);

#endif
