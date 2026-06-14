#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "clip_store.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wctype.h>

#define INITIAL_CAPACITY 16
#define MAX_CLIP_TEXT_CHARS 20000

static wchar_t *dup_wide(const wchar_t *text) {
    size_t len;
    wchar_t *copy;

    if (text == NULL) {
        text = L"";
    }

    len = wcslen(text);
    copy = (wchar_t *)malloc((len + 1) * sizeof(wchar_t));
    if (copy == NULL) {
        return NULL;
    }

    memcpy(copy, text, (len + 1) * sizeof(wchar_t));
    return copy;
}

static int ensure_capacity(ClipStore *store, size_t needed) {
    size_t new_capacity;
    ClipItem *new_items;

    if (store->capacity >= needed) {
        return 1;
    }

    new_capacity = store->capacity == 0 ? INITIAL_CAPACITY : store->capacity * 2;
    while (new_capacity < needed) {
        new_capacity *= 2;
    }

    new_items = (ClipItem *)realloc(store->items, new_capacity * sizeof(ClipItem));
    if (new_items == NULL) {
        return 0;
    }

    store->items = new_items;
    store->capacity = new_capacity;
    return 1;
}

static void free_item(ClipItem *item) {
    free(item->text);
    item->text = NULL;
}

static int compare_items(const ClipItem *a, const ClipItem *b) {
    if (a->pinned != b->pinned) {
        return b->pinned - a->pinned;
    }
    if (a->created_at < b->created_at) {
        return 1;
    }
    if (a->created_at > b->created_at) {
        return -1;
    }
    if (a->id < b->id) {
        return 1;
    }
    if (a->id > b->id) {
        return -1;
    }
    return 0;
}

static int qsort_compare_items(const void *left, const void *right) {
    const ClipItem *a = (const ClipItem *)left;
    const ClipItem *b = (const ClipItem *)right;
    return compare_items(a, b);
}

static void sort_store(ClipStore *store) {
    if (store->count > 1) {
        qsort(store->items, store->count, sizeof(ClipItem), qsort_compare_items);
    }
}

static int is_empty_text(const wchar_t *text) {
    while (text != NULL && *text != L'\0') {
        if (!iswspace(*text)) {
            return 0;
        }
        text++;
    }
    return 1;
}

static void normalize_clip_text(wchar_t *text) {
    size_t read_pos = 0;
    size_t write_pos = 0;
    size_t len;

    if (text == NULL) {
        return;
    }

    len = wcslen(text);
    if (len > MAX_CLIP_TEXT_CHARS) {
        text[MAX_CLIP_TEXT_CHARS] = L'\0';
        len = MAX_CLIP_TEXT_CHARS;
    }

    while (read_pos < len) {
        wchar_t ch = text[read_pos++];
        if (ch == L'\r') {
            if (read_pos < len && text[read_pos] == L'\n') {
                read_pos++;
            }
            text[write_pos++] = L'\n';
        } else {
            text[write_pos++] = ch;
        }
    }
    text[write_pos] = L'\0';
}

static char *wide_to_utf8(const wchar_t *text) {
    int needed;
    char *utf8;

    if (text == NULL) {
        text = L"";
    }

    needed = WideCharToMultiByte(CP_UTF8, 0, text, -1, NULL, 0, NULL, NULL);
    if (needed <= 0) {
        return NULL;
    }

    utf8 = (char *)malloc((size_t)needed);
    if (utf8 == NULL) {
        return NULL;
    }

    if (WideCharToMultiByte(CP_UTF8, 0, text, -1, utf8, needed, NULL, NULL) <= 0) {
        free(utf8);
        return NULL;
    }

    return utf8;
}

static wchar_t *utf8_to_wide(const char *text) {
    int needed;
    wchar_t *wide;

    if (text == NULL) {
        text = "";
    }

    needed = MultiByteToWideChar(CP_UTF8, 0, text, -1, NULL, 0);
    if (needed <= 0) {
        needed = MultiByteToWideChar(CP_ACP, 0, text, -1, NULL, 0);
        if (needed <= 0) {
            return NULL;
        }
        wide = (wchar_t *)malloc((size_t)needed * sizeof(wchar_t));
        if (wide == NULL) {
            return NULL;
        }
        if (MultiByteToWideChar(CP_ACP, 0, text, -1, wide, needed) <= 0) {
            free(wide);
            return NULL;
        }
        return wide;
    }

    wide = (wchar_t *)malloc((size_t)needed * sizeof(wchar_t));
    if (wide == NULL) {
        return NULL;
    }

    if (MultiByteToWideChar(CP_UTF8, 0, text, -1, wide, needed) <= 0) {
        free(wide);
        return NULL;
    }

    return wide;
}

static int append_escaped_utf8(FILE *file, const wchar_t *text) {
    char *utf8;
    const unsigned char *p;

    utf8 = wide_to_utf8(text);
    if (utf8 == NULL) {
        return 0;
    }

    for (p = (const unsigned char *)utf8; *p != '\0'; p++) {
        switch (*p) {
        case '\\':
            fputs("\\\\", file);
            break;
        case '\n':
            fputs("\\n", file);
            break;
        case '\t':
            fputs("\\t", file);
            break;
        default:
            fputc((int)*p, file);
            break;
        }
    }

    free(utf8);
    return ferror(file) == 0;
}

static char *unescape_tsv_text(const char *text) {
    size_t len;
    char *out;
    size_t r = 0;
    size_t w = 0;

    if (text == NULL) {
        text = "";
    }

    len = strlen(text);
    out = (char *)malloc(len + 1);
    if (out == NULL) {
        return NULL;
    }

    while (text[r] != '\0') {
        if (text[r] == '\\' && text[r + 1] != '\0') {
            r++;
            if (text[r] == 'n') {
                out[w++] = '\n';
            } else if (text[r] == 't') {
                out[w++] = '\t';
            } else {
                out[w++] = text[r];
            }
            r++;
        } else {
            out[w++] = text[r++];
        }
    }

    out[w] = '\0';
    return out;
}

static int contains_case_wide(const wchar_t *text, const wchar_t *keyword) {
    size_t text_len;
    size_t key_len;
    size_t i;

    if (keyword == NULL || *keyword == L'\0') {
        return 1;
    }
    if (text == NULL) {
        return 0;
    }

    text_len = wcslen(text);
    key_len = wcslen(keyword);
    if (key_len > text_len) {
        return 0;
    }

    for (i = 0; i + key_len <= text_len; i++) {
        size_t j;
        for (j = 0; j < key_len; j++) {
            if (towlower(text[i + j]) != towlower(keyword[j])) {
                break;
            }
        }
        if (j == key_len) {
            return 1;
        }
    }

    return 0;
}

static int search_result_append(ClipSearchResult *result, unsigned long id) {
    unsigned long *new_ids;
    size_t new_capacity;

    if (result->count >= result->capacity) {
        new_capacity = result->capacity == 0 ? INITIAL_CAPACITY : result->capacity * 2;
        new_ids = (unsigned long *)realloc(result->ids, new_capacity * sizeof(unsigned long));
        if (new_ids == NULL) {
            return 0;
        }
        result->ids = new_ids;
        result->capacity = new_capacity;
    }

    result->ids[result->count++] = id;
    return 1;
}

void store_init(ClipStore *store) {
    store->items = NULL;
    store->count = 0;
    store->capacity = 0;
    store->next_id = 1;
}

void store_free(ClipStore *store) {
    store_clear(store);
    free(store->items);
    store->items = NULL;
    store->capacity = 0;
    store->next_id = 1;
}

int store_load(ClipStore *store, const wchar_t *path) {
    FILE *file;
    char line[262144];
    unsigned long max_id = 0;

    file = _wfopen(path, L"rb");
    if (file == NULL) {
        return errno == ENOENT;
    }

    while (fgets(line, sizeof(line), file) != NULL) {
        char *fields[5];
        char *cursor = line;
        char *raw_text;
        char *unescaped;
        wchar_t *wide_text;
        ClipItem item;
        int i;

        line[strcspn(line, "\r\n")] = '\0';
        if (line[0] == '\0' || line[0] == '#') {
            continue;
        }

        for (i = 0; i < 4; i++) {
            fields[i] = cursor;
            cursor = strchr(cursor, '\t');
            if (cursor == NULL) {
                break;
            }
            *cursor = '\0';
            cursor++;
        }
        if (i != 4) {
            continue;
        }
        fields[4] = cursor;

        raw_text = fields[4];
        unescaped = unescape_tsv_text(raw_text);
        if (unescaped == NULL) {
            fclose(file);
            return 0;
        }

        wide_text = utf8_to_wide(unescaped);
        free(unescaped);
        if (wide_text == NULL) {
            fclose(file);
            return 0;
        }

        item.id = strtoul(fields[0], NULL, 10);
        item.created_at = (time_t)strtoll(fields[1], NULL, 10);
        item.pinned = atoi(fields[2]) != 0;
        item.copy_count = (unsigned int)strtoul(fields[3], NULL, 10);
        item.text = wide_text;

        if (item.id == 0 || item.text[0] == L'\0') {
            free_item(&item);
            continue;
        }

        if (!ensure_capacity(store, store->count + 1)) {
            free_item(&item);
            fclose(file);
            return 0;
        }
        store->items[store->count++] = item;
        if (item.id > max_id) {
            max_id = item.id;
        }
    }

    fclose(file);
    store->next_id = max_id + 1;
    if (store->next_id == 0) {
        store->next_id = 1;
    }
    sort_store(store);
    return 1;
}

int store_save(const ClipStore *store, const wchar_t *path) {
    FILE *file;
    size_t i;

    file = _wfopen(path, L"wb");
    if (file == NULL) {
        return 0;
    }

    fputs("# id\tcreated_at\tpinned\tcopy_count\ttext\n", file);
    for (i = 0; i < store->count; i++) {
        const ClipItem *item = &store->items[i];
        fprintf(file, "%lu\t%lld\t%d\t%u\t",
                item->id,
                (long long)item->created_at,
                item->pinned ? 1 : 0,
                item->copy_count);
        if (!append_escaped_utf8(file, item->text)) {
            fclose(file);
            return 0;
        }
        fputc('\n', file);
    }

    fclose(file);
    return 1;
}

ClipItem *store_add_text(ClipStore *store, const wchar_t *text) {
    ClipItem item;
    wchar_t *copy;
    size_t i;

    if (is_empty_text(text)) {
        return NULL;
    }

    copy = dup_wide(text);
    if (copy == NULL) {
        return NULL;
    }
    normalize_clip_text(copy);

    if (is_empty_text(copy)) {
        free(copy);
        return NULL;
    }

    for (i = 0; i < store->count; i++) {
        if (wcscmp(store->items[i].text, copy) == 0) {
            unsigned long id = store->items[i].id;
            store->items[i].created_at = time(NULL);
            free(copy);
            sort_store(store);
            return store_find(store, id);
        }
    }

    if (!ensure_capacity(store, store->count + 1)) {
        free(copy);
        return NULL;
    }

    item.id = store->next_id++;
    item.created_at = time(NULL);
    item.pinned = 0;
    item.copy_count = 0;
    item.text = copy;

    store->items[store->count++] = item;
    sort_store(store);
    return store_find(store, item.id);
}

ClipItem *store_find(ClipStore *store, unsigned long id) {
    size_t i;
    for (i = 0; i < store->count; i++) {
        if (store->items[i].id == id) {
            return &store->items[i];
        }
    }
    return NULL;
}

const ClipItem *store_find_const(const ClipStore *store, unsigned long id) {
    size_t i;
    for (i = 0; i < store->count; i++) {
        if (store->items[i].id == id) {
            return &store->items[i];
        }
    }
    return NULL;
}

int store_delete(ClipStore *store, unsigned long id) {
    size_t i;

    for (i = 0; i < store->count; i++) {
        if (store->items[i].id == id) {
            free_item(&store->items[i]);
            if (i + 1 < store->count) {
                memmove(&store->items[i], &store->items[i + 1],
                        (store->count - i - 1) * sizeof(ClipItem));
            }
            store->count--;
            return 1;
        }
    }

    return 0;
}

int store_toggle_pin(ClipStore *store, unsigned long id) {
    ClipItem *item = store_find(store, id);
    if (item == NULL) {
        return 0;
    }

    item->pinned = !item->pinned;
    sort_store(store);
    return 1;
}

size_t store_prune_to_limit(ClipStore *store, size_t max_items) {
    size_t removed = 0;

    if (max_items == 0) {
        return 0;
    }

    sort_store(store);
    while (store->count > max_items) {
        size_t i;
        size_t remove_index = store->count;

        for (i = store->count; i > 0; i--) {
            if (!store->items[i - 1].pinned) {
                remove_index = i - 1;
                break;
            }
        }

        if (remove_index == store->count) {
            break;
        }

        free_item(&store->items[remove_index]);
        if (remove_index + 1 < store->count) {
            memmove(&store->items[remove_index], &store->items[remove_index + 1],
                    (store->count - remove_index - 1) * sizeof(ClipItem));
        }
        store->count--;
        removed++;
    }

    return removed;
}

void store_clear(ClipStore *store) {
    size_t i;

    for (i = 0; i < store->count; i++) {
        free_item(&store->items[i]);
    }
    store->count = 0;
}

void store_increment_copy_count(ClipStore *store, unsigned long id) {
    ClipItem *item = store_find(store, id);
    if (item != NULL) {
        item->copy_count++;
    }
}

void search_result_init(ClipSearchResult *result) {
    result->ids = NULL;
    result->count = 0;
    result->capacity = 0;
}

void search_result_free(ClipSearchResult *result) {
    free(result->ids);
    result->ids = NULL;
    result->count = 0;
    result->capacity = 0;
}

int store_search(const ClipStore *store, const wchar_t *keyword, ClipSearchResult *result) {
    size_t i;

    result->count = 0;
    for (i = 0; i < store->count; i++) {
        if (contains_case_wide(store->items[i].text, keyword)) {
            if (!search_result_append(result, store->items[i].id)) {
                return 0;
            }
        }
    }

    return 1;
}

const wchar_t *store_preview_text(const wchar_t *text, wchar_t *buffer, size_t buffer_count) {
    size_t i;
    size_t out = 0;

    if (buffer_count == 0) {
        return L"";
    }

    if (text == NULL) {
        buffer[0] = L'\0';
        return buffer;
    }

    for (i = 0; text[i] != L'\0' && out + 1 < buffer_count; i++) {
        if (text[i] == L'\n' || text[i] == L'\r' || text[i] == L'\t') {
            if (out + 1 >= buffer_count) {
                break;
            }
            buffer[out++] = L' ';
        } else {
            buffer[out++] = text[i];
        }

        if (out >= 90 && out + 4 < buffer_count && text[i + 1] != L'\0') {
            buffer[out++] = L'.';
            buffer[out++] = L'.';
            buffer[out++] = L'.';
            break;
        }
    }

    buffer[out] = L'\0';
    return buffer;
}

void store_format_time(time_t value, wchar_t *buffer, size_t buffer_count) {
    struct tm tm_value;

    if (buffer_count == 0) {
        return;
    }

    if (localtime_s(&tm_value, &value) != 0) {
        buffer[0] = L'\0';
        return;
    }

    wcsftime(buffer, buffer_count, L"%Y-%m-%d %H:%M", &tm_value);
}
