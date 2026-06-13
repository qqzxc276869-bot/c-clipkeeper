#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "clipboard_win.h"

#include <stdlib.h>
#include <string.h>

static wchar_t *dup_clipboard_text(const wchar_t *text) {
    size_t len;
    wchar_t *copy;

    if (text == NULL) {
        return NULL;
    }

    len = wcslen(text);
    copy = (wchar_t *)malloc((len + 1) * sizeof(wchar_t));
    if (copy == NULL) {
        return NULL;
    }

    memcpy(copy, text, (len + 1) * sizeof(wchar_t));
    return copy;
}

wchar_t *clipboard_read_text(HWND owner) {
    HANDLE data;
    const wchar_t *locked;
    wchar_t *copy;

    if (!IsClipboardFormatAvailable(CF_UNICODETEXT)) {
        return NULL;
    }

    if (!OpenClipboard(owner)) {
        return NULL;
    }

    data = GetClipboardData(CF_UNICODETEXT);
    if (data == NULL) {
        CloseClipboard();
        return NULL;
    }

    locked = (const wchar_t *)GlobalLock(data);
    if (locked == NULL) {
        CloseClipboard();
        return NULL;
    }

    copy = dup_clipboard_text(locked);
    GlobalUnlock(data);
    CloseClipboard();
    return copy;
}

int clipboard_write_text(HWND owner, const wchar_t *text) {
    size_t len;
    size_t bytes;
    HGLOBAL memory;
    wchar_t *target;

    if (text == NULL) {
        text = L"";
    }

    len = wcslen(text);
    bytes = (len + 1) * sizeof(wchar_t);
    memory = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (memory == NULL) {
        return 0;
    }

    target = (wchar_t *)GlobalLock(memory);
    if (target == NULL) {
        GlobalFree(memory);
        return 0;
    }

    memcpy(target, text, bytes);
    GlobalUnlock(memory);

    if (!OpenClipboard(owner)) {
        GlobalFree(memory);
        return 0;
    }

    if (!EmptyClipboard()) {
        CloseClipboard();
        GlobalFree(memory);
        return 0;
    }

    if (SetClipboardData(CF_UNICODETEXT, memory) == NULL) {
        CloseClipboard();
        GlobalFree(memory);
        return 0;
    }

    CloseClipboard();
    return 1;
}
