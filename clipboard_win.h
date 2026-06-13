#ifndef CLIPBOARD_WIN_H
#define CLIPBOARD_WIN_H

#include <windows.h>
#include <wchar.h>

wchar_t *clipboard_read_text(HWND owner);
int clipboard_write_text(HWND owner, const wchar_t *text);

#endif
