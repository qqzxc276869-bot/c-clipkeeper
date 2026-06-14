#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>

#include "clip_store.h"
#include "clipboard_win.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <wctype.h>

#ifndef EM_SETCUEBANNER
#define EM_SETCUEBANNER 0x1501
#endif

#define APP_TITLE L"ClipKeeper 剪贴板管家"

#define IDC_SEARCH_EDIT 1001
#define IDC_HISTORY_LIST 1002
#define IDC_PREVIEW_EDIT 1003
#define IDC_COPY_BUTTON 1004
#define IDC_PIN_BUTTON 1005
#define IDC_DELETE_BUTTON 1006
#define IDC_CLEAR_BUTTON 1007
#define IDC_PAUSE_CHECK 1008
#define IDC_STATUS_TEXT 1009
#define IDC_SEARCH_LABEL 1010
#define IDC_PREVIEW_LABEL 1011
#define IDC_TITLE_TEXT 1012
#define IDC_SUBTITLE_TEXT 1013
#define IDC_HISTORY_LABEL 1014
#define IDC_STARTUP_CHECK 1015
#define IDC_THEME_CHECK 1016
#define IDC_START_HIDDEN_CHECK 1017
#define IDC_HOTKEY_CHECK 1018
#define IDC_HOTKEY_DISPLAY 1019
#define IDC_HOTKEY_SET_BUTTON 1020
#define IDC_FILTER_SENSITIVE_CHECK 1021
#define IDC_MAX_HISTORY_LABEL 1022
#define IDC_MAX_HISTORY_EDIT 1023
#define IDC_QUICK_SEARCH_BUTTON 1024
#define IDC_QUICK_SEARCH_EDIT 2001
#define IDC_QUICK_SEARCH_LIST 2002

#define WM_TRAYICON (WM_APP + 1)
#define HOTKEY_SHOW_WINDOW 1
#define TRAY_UID 100
#define ID_TRAY_SHOW 4001
#define ID_TRAY_TOGGLE_PAUSE 4002
#define ID_TRAY_EXIT 4003
#define ID_TRAY_QUICK_SEARCH 4004

#define RUN_KEY_PATH L"Software\\Microsoft\\Windows\\CurrentVersion\\Run"
#define RUN_VALUE_NAME L"ClipKeeper"
#define QUICK_SEARCH_CLASS L"ClipKeeperQuickSearchWindowClass"
#define DEFAULT_HISTORY_LIMIT 500
#define MIN_HISTORY_LIMIT 20
#define MAX_HISTORY_LIMIT 5000

typedef struct {
    ClipStore store;
    ClipSearchResult search;
    wchar_t data_path[MAX_PATH];
    wchar_t settings_path[MAX_PATH];
    HWND hwnd;
    HWND search_edit;
    HWND history_list;
    HWND preview_edit;
    HWND copy_button;
    HWND pin_button;
    HWND delete_button;
    HWND clear_button;
    HWND pause_check;
    HWND status_text;
    HWND search_label;
    HWND preview_label;
    HWND title_text;
    HWND subtitle_text;
    HWND history_label;
    HWND startup_check;
    HWND theme_check;
    HWND start_hidden_check;
    HWND hotkey_check;
    HWND hotkey_display;
    HWND hotkey_set_button;
    HWND filter_sensitive_check;
    HWND max_history_label;
    HWND max_history_edit;
    HWND quick_search_button;
    HWND quick_hwnd;
    HWND quick_edit;
    HWND quick_list;
    HFONT font;
    HFONT title_font;
    HFONT small_font;
    HBRUSH background_brush;
    HBRUSH surface_brush;
    RECT hero_rect;
    RECT search_rect;
    RECT left_panel_rect;
    RECT right_panel_rect;
    RECT action_rect;
    RECT status_rect;
    int dark_theme;
    int start_hidden;
    int launched_hidden;
    int pause_capture;
    int filter_sensitive;
    size_t max_history;
    int hotkey_enabled;
    UINT hotkey_modifiers;
    UINT hotkey_vk;
    int hotkey_capture;
    int tray_added;
    int hotkey_registered;
    int allow_exit;
    wchar_t *last_written_text;
    ClipSearchResult quick_search;
} AppState;

static AppState g_app;

static void update_status(void);
static void redraw_opaque_control(HWND hwnd);
static void show_quick_search(void);
static LRESULT CALLBACK quick_search_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);

static void set_status(const wchar_t *text) {
    if (g_app.status_text == NULL || !IsWindow(g_app.status_text)) {
        return;
    }

    InvalidateRect(g_app.status_text, NULL, TRUE);
    SetWindowTextW(g_app.status_text, text);
    InvalidateRect(g_app.status_text, NULL, TRUE);
    UpdateWindow(g_app.status_text);
}

static void set_hotkey_display_text(const wchar_t *text) {
    if (g_app.hotkey_display == NULL || !IsWindow(g_app.hotkey_display)) {
        return;
    }

    SetWindowTextW(g_app.hotkey_display, text);
    redraw_opaque_control(g_app.hotkey_display);
}

static void get_app_directory(wchar_t *out, size_t out_count) {
    wchar_t *slash;

    if (out_count == 0) {
        return;
    }

    GetModuleFileNameW(NULL, out, (DWORD)out_count);
    out[out_count - 1] = L'\0';
    slash = wcsrchr(out, L'\\');
    if (slash != NULL) {
        *slash = L'\0';
    }
}

static void build_app_paths(void) {
    wchar_t app_dir[MAX_PATH];
    get_app_directory(app_dir, MAX_PATH);
    swprintf(g_app.data_path, MAX_PATH, L"%ls\\clips.tsv", app_dir);
    swprintf(g_app.settings_path, MAX_PATH, L"%ls\\clipkeeper.ini", app_dir);
}

static COLORREF theme_background_color(void) {
    return g_app.dark_theme ? RGB(18, 24, 38) : RGB(246, 248, 252);
}

static COLORREF theme_surface_color(void) {
    return g_app.dark_theme ? RGB(31, 41, 55) : RGB(255, 255, 255);
}

static COLORREF theme_text_color(void) {
    return g_app.dark_theme ? RGB(232, 238, 247) : RGB(31, 41, 55);
}

static COLORREF theme_muted_text_color(void) {
    return g_app.dark_theme ? RGB(156, 170, 191) : RGB(91, 103, 125);
}

static COLORREF theme_title_text_color(void) {
    return g_app.dark_theme ? RGB(248, 250, 252) : RGB(24, 33, 51);
}

static void refresh_theme_brushes(void) {
    HBRUSH new_background = CreateSolidBrush(theme_background_color());
    HBRUSH new_surface = CreateSolidBrush(theme_surface_color());

    if (g_app.background_brush != NULL) {
        DeleteObject(g_app.background_brush);
    }
    if (g_app.surface_brush != NULL) {
        DeleteObject(g_app.surface_brush);
    }

    g_app.background_brush = new_background;
    g_app.surface_brush = new_surface;
}

static void redraw_control(HWND hwnd) {
    if (hwnd != NULL && IsWindow(hwnd)) {
        RedrawWindow(hwnd, NULL, NULL, RDW_INVALIDATE | RDW_UPDATENOW);
    }
}

static void redraw_opaque_control(HWND hwnd) {
    if (hwnd != NULL && IsWindow(hwnd)) {
        RedrawWindow(hwnd, NULL, NULL,
                     RDW_INVALIDATE | RDW_ERASE | RDW_ERASENOW | RDW_UPDATENOW);
    }
}

static void sync_switch_check(HWND hwnd, int checked) {
    if (hwnd != NULL && IsWindow(hwnd)) {
        SendMessageW(hwnd, BM_SETCHECK, checked ? BST_CHECKED : BST_UNCHECKED, 0);
        redraw_control(hwnd);
    }
}

static COLORREF blend_color(COLORREF a, COLORREF b, int percent_b) {
    int percent_a = 100 - percent_b;
    int r = (GetRValue(a) * percent_a + GetRValue(b) * percent_b) / 100;
    int g = (GetGValue(a) * percent_a + GetGValue(b) * percent_b) / 100;
    int bl = (GetBValue(a) * percent_a + GetBValue(b) * percent_b) / 100;
    return RGB(r, g, bl);
}

static void fill_solid_rect(HDC hdc, const RECT *rect, COLORREF color) {
    HBRUSH brush = CreateSolidBrush(color);
    FillRect(hdc, rect, brush);
    DeleteObject(brush);
}

static void draw_vertical_gradient(HDC hdc, const RECT *rect,
                                   COLORREF top, COLORREF bottom) {
    int y;
    int height = rect->bottom - rect->top;

    if (height <= 0) {
        return;
    }

    for (y = 0; y < height; y++) {
        RECT line = { rect->left, rect->top + y, rect->right, rect->top + y + 1 };
        COLORREF color = blend_color(top, bottom, (y * 100) / height);
        fill_solid_rect(hdc, &line, color);
    }
}

static void draw_round_rect(HDC hdc, const RECT *rect, int radius,
                            COLORREF fill, COLORREF border, int border_width) {
    HBRUSH brush = CreateSolidBrush(fill);
    HPEN pen = CreatePen(PS_SOLID, border_width, border);
    HGDIOBJ old_brush = SelectObject(hdc, brush);
    HGDIOBJ old_pen = SelectObject(hdc, pen);

    RoundRect(hdc, rect->left, rect->top, rect->right, rect->bottom,
              radius, radius);

    SelectObject(hdc, old_pen);
    SelectObject(hdc, old_brush);
    DeleteObject(pen);
    DeleteObject(brush);
}

static void draw_glass_panel(HDC hdc, const RECT *rect, int strong) {
    RECT shadow = *rect;
    RECT shine = *rect;
    COLORREF fill;
    COLORREF border;
    COLORREF shadow_color;
    COLORREF shine_color;
    HPEN shine_pen;
    HGDIOBJ old_pen;

    OffsetRect(&shadow, 0, 8);
    shadow_color = g_app.dark_theme ? RGB(9, 12, 22) : RGB(216, 225, 241);
    draw_round_rect(hdc, &shadow, 26, shadow_color, shadow_color, 1);

    fill = g_app.dark_theme
               ? (strong ? RGB(37, 49, 70) : RGB(30, 40, 59))
               : (strong ? RGB(255, 255, 255) : RGB(250, 253, 255));
    border = g_app.dark_theme ? RGB(83, 102, 132) : RGB(255, 255, 255);
    draw_round_rect(hdc, rect, 26, fill, border, 1);

    shine.bottom = shine.top + 34;
    shine.left += 16;
    shine.right -= 16;
    shine_color = g_app.dark_theme ? RGB(72, 89, 118) : RGB(255, 255, 255);
    shine_pen = CreatePen(PS_SOLID, 1, shine_color);
    old_pen = SelectObject(hdc, shine_pen);
    MoveToEx(hdc, shine.left + 8, shine.top + 10, NULL);
    LineTo(hdc, shine.right - 8, shine.top + 10);
    SelectObject(hdc, old_pen);
    DeleteObject(shine_pen);
}

static void draw_liquid_background(HDC hdc, const RECT *rect) {
    COLORREF top = g_app.dark_theme ? RGB(13, 18, 31) : RGB(236, 245, 255);
    COLORREF mid = g_app.dark_theme ? RGB(28, 32, 56) : RGB(249, 244, 255);
    COLORREF bottom = g_app.dark_theme ? RGB(11, 17, 29) : RGB(240, 251, 249);
    RECT upper = *rect;
    RECT lower = *rect;
    HPEN pen;
    HGDIOBJ old_pen;

    upper.bottom = rect->top + (rect->bottom - rect->top) / 2;
    lower.top = upper.bottom;
    draw_vertical_gradient(hdc, &upper, top, mid);
    draw_vertical_gradient(hdc, &lower, mid, bottom);

    pen = CreatePen(PS_SOLID, 1, g_app.dark_theme ? RGB(47, 62, 91) : RGB(255, 255, 255));
    old_pen = SelectObject(hdc, pen);
    MoveToEx(hdc, rect->left + 40, rect->top + 88, NULL);
    LineTo(hdc, rect->right - 80, rect->top + 24);
    MoveToEx(hdc, rect->left + 120, rect->bottom - 48, NULL);
    LineTo(hdc, rect->right - 32, rect->bottom - 140);
    SelectObject(hdc, old_pen);
    DeleteObject(pen);
}

static void draw_window_chrome(HDC hdc) {
    RECT rc;
    GetClientRect(g_app.hwnd, &rc);
    draw_liquid_background(hdc, &rc);
    draw_glass_panel(hdc, &g_app.hero_rect, 1);
    draw_glass_panel(hdc, &g_app.search_rect, 0);
    draw_glass_panel(hdc, &g_app.left_panel_rect, 1);
    draw_glass_panel(hdc, &g_app.right_panel_rect, 1);
    draw_glass_panel(hdc, &g_app.action_rect, 0);
}

static void save_settings(void) {
    FILE *file = _wfopen(g_app.settings_path, L"wb");
    if (file == NULL) {
        return;
    }

    fprintf(file, "theme=%s\n", g_app.dark_theme ? "dark" : "light");
    fprintf(file, "start_hidden=%d\n", g_app.start_hidden ? 1 : 0);
    fprintf(file, "filter_sensitive=%d\n", g_app.filter_sensitive ? 1 : 0);
    fprintf(file, "max_history=%u\n", (unsigned int)g_app.max_history);
    fprintf(file, "hotkey_enabled=%d\n", g_app.hotkey_enabled ? 1 : 0);
    fprintf(file, "hotkey_modifiers=%u\n", (unsigned int)g_app.hotkey_modifiers);
    fprintf(file, "hotkey_vk=%u\n", (unsigned int)g_app.hotkey_vk);
    fclose(file);
}

static size_t clamp_history_limit(size_t value) {
    if (value < MIN_HISTORY_LIMIT) {
        return MIN_HISTORY_LIMIT;
    }
    if (value > MAX_HISTORY_LIMIT) {
        return MAX_HISTORY_LIMIT;
    }
    return value;
}

static void load_settings(void) {
    FILE *file = _wfopen(g_app.settings_path, L"rb");
    char line[128];

    g_app.dark_theme = 0;
    g_app.start_hidden = 0;
    g_app.filter_sensitive = 1;
    g_app.max_history = DEFAULT_HISTORY_LIMIT;
    g_app.hotkey_enabled = 0;
    g_app.hotkey_modifiers = MOD_CONTROL | MOD_SHIFT;
    g_app.hotkey_vk = 'V';
    if (file == NULL) {
        return;
    }

    while (fgets(line, sizeof(line), file) != NULL) {
        if (strncmp(line, "theme=dark", 10) == 0) {
            g_app.dark_theme = 1;
            continue;
        }
        if (strncmp(line, "theme=light", 11) == 0) {
            g_app.dark_theme = 0;
            continue;
        }
        if (strncmp(line, "start_hidden=1", 14) == 0) {
            g_app.start_hidden = 1;
            continue;
        }
        if (strncmp(line, "start_hidden=0", 14) == 0) {
            g_app.start_hidden = 0;
            continue;
        }
        if (strncmp(line, "filter_sensitive=1", 18) == 0) {
            g_app.filter_sensitive = 1;
            continue;
        }
        if (strncmp(line, "filter_sensitive=0", 18) == 0) {
            g_app.filter_sensitive = 0;
            continue;
        }
        if (strncmp(line, "max_history=", 12) == 0) {
            g_app.max_history = clamp_history_limit((size_t)strtoul(line + 12, NULL, 10));
            continue;
        }
        if (strncmp(line, "hotkey_enabled=1", 16) == 0) {
            g_app.hotkey_enabled = 1;
            continue;
        }
        if (strncmp(line, "hotkey_enabled=0", 16) == 0) {
            g_app.hotkey_enabled = 0;
            continue;
        }
        if (strncmp(line, "hotkey_modifiers=", 17) == 0) {
            g_app.hotkey_modifiers = (UINT)strtoul(line + 17, NULL, 10);
            continue;
        }
        if (strncmp(line, "hotkey_vk=", 10) == 0) {
            g_app.hotkey_vk = (UINT)strtoul(line + 10, NULL, 10);
            continue;
        }
        if (strncmp(line, "hotkey_index=", 13) == 0) {
            int index = atoi(line + 13);
            if (index == 1) {
                g_app.hotkey_modifiers = MOD_CONTROL | MOD_ALT;
                g_app.hotkey_vk = 'V';
            } else if (index == 2) {
                g_app.hotkey_modifiers = MOD_CONTROL | MOD_SHIFT;
                g_app.hotkey_vk = 'C';
            } else if (index == 3) {
                g_app.hotkey_modifiers = MOD_CONTROL | MOD_ALT;
                g_app.hotkey_vk = 'C';
            } else if (index == 4) {
                g_app.hotkey_modifiers = MOD_CONTROL | MOD_SHIFT;
                g_app.hotkey_vk = VK_SPACE;
            }
            continue;
        }
    }

    fclose(file);
}

static void apply_theme_to_controls(void) {
    if (g_app.theme_check != NULL) {
        SetWindowTextW(g_app.theme_check, g_app.dark_theme ? L"夜间模式" : L"白天模式");
        sync_switch_check(g_app.theme_check, g_app.dark_theme);
    }
    if (g_app.start_hidden_check != NULL) {
        sync_switch_check(g_app.start_hidden_check, g_app.start_hidden);
    }
    sync_switch_check(g_app.pause_check, g_app.pause_capture);
    sync_switch_check(g_app.hotkey_check, g_app.hotkey_enabled);
    sync_switch_check(g_app.filter_sensitive_check, g_app.filter_sensitive);

    InvalidateRect(g_app.hwnd, NULL, TRUE);
    InvalidateRect(g_app.search_edit, NULL, TRUE);
    InvalidateRect(g_app.history_list, NULL, TRUE);
    InvalidateRect(g_app.preview_edit, NULL, TRUE);
    InvalidateRect(g_app.hotkey_display, NULL, TRUE);
    InvalidateRect(g_app.max_history_edit, NULL, TRUE);
    redraw_control(g_app.copy_button);
    redraw_control(g_app.pin_button);
    redraw_control(g_app.delete_button);
    redraw_control(g_app.clear_button);
    redraw_control(g_app.hotkey_set_button);
    redraw_control(g_app.quick_search_button);
    redraw_control(g_app.quick_hwnd);
    redraw_opaque_control(g_app.quick_edit);
    redraw_opaque_control(g_app.quick_list);
}

static void sync_history_settings_controls(void) {
    wchar_t limit_text[32];

    if (g_app.filter_sensitive_check != NULL) {
        sync_switch_check(g_app.filter_sensitive_check, g_app.filter_sensitive);
    }
    if (g_app.max_history_edit != NULL) {
        swprintf(limit_text, 32, L"%u", (unsigned int)g_app.max_history);
        SetWindowTextW(g_app.max_history_edit, limit_text);
        redraw_opaque_control(g_app.max_history_edit);
    }
}

static int contains_case_wide_local(const wchar_t *text, const wchar_t *keyword) {
    size_t text_len;
    size_t key_len;
    size_t i;

    if (keyword == NULL || keyword[0] == L'\0') {
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

static int has_sensitive_keyword(const wchar_t *text) {
    static const wchar_t *keywords[] = {
        L"密码", L"验证码", L"口令", L"令牌", L"密钥", L"私钥",
        L"银行卡", L"身份证", L"password", L"passwd", L"pwd",
        L"token", L"secret", L"api_key", L"apikey", L"access_key",
        L"authorization", L"bearer", L"private key"
    };
    size_t i;

    for (i = 0; i < sizeof(keywords) / sizeof(keywords[0]); i++) {
        if (contains_case_wide_local(text, keywords[i])) {
            return 1;
        }
    }
    return 0;
}

static int looks_like_short_code(const wchar_t *text) {
    size_t digits = 0;
    size_t chars = 0;

    while (text != NULL && *text != L'\0') {
        if (!iswspace(*text)) {
            chars++;
            if (!iswdigit(*text)) {
                return 0;
            }
            digits++;
        }
        text++;
    }

    return digits >= 4 && digits <= 8 && chars == digits;
}

static int luhn_valid_digits(const wchar_t *digits, size_t count) {
    int sum = 0;
    int double_digit = 0;
    size_t i;

    if (count < 13 || count > 19) {
        return 0;
    }

    for (i = count; i > 0; i--) {
        int value = (int)(digits[i - 1] - L'0');
        if (double_digit) {
            value *= 2;
            if (value > 9) {
                value -= 9;
            }
        }
        sum += value;
        double_digit = !double_digit;
    }

    return sum % 10 == 0;
}

static int has_bank_card_number(const wchar_t *text) {
    wchar_t digits[32];
    size_t count = 0;

    while (text != NULL && *text != L'\0') {
        if (iswdigit(*text)) {
            if (count < sizeof(digits) / sizeof(digits[0])) {
                digits[count++] = *text;
            }
        } else {
            if (luhn_valid_digits(digits, count)) {
                return 1;
            }
            count = 0;
        }
        text++;
    }

    return luhn_valid_digits(digits, count);
}

static int has_long_secret_token(const wchar_t *text) {
    size_t run = 0;
    int has_letter = 0;
    int has_digit = 0;

    while (text != NULL && *text != L'\0') {
        wchar_t ch = *text++;
        if (iswalnum(ch) || ch == L'_' || ch == L'-' || ch == L'.' ||
            ch == L'/' || ch == L'+' || ch == L'=') {
            run++;
            if (iswalpha(ch)) {
                has_letter = 1;
            } else if (iswdigit(ch)) {
                has_digit = 1;
            }
            if (run >= 32 && has_letter && has_digit) {
                return 1;
            }
        } else {
            run = 0;
            has_letter = 0;
            has_digit = 0;
        }
    }

    return 0;
}

static int looks_sensitive_clip_text(const wchar_t *text) {
    return has_sensitive_keyword(text) ||
           looks_like_short_code(text) ||
           has_bank_card_number(text) ||
           has_long_secret_token(text);
}

static int is_modifier_vk(UINT vk) {
    return vk == VK_CONTROL || vk == VK_LCONTROL || vk == VK_RCONTROL ||
           vk == VK_SHIFT || vk == VK_LSHIFT || vk == VK_RSHIFT ||
           vk == VK_MENU || vk == VK_LMENU || vk == VK_RMENU ||
           vk == VK_LWIN || vk == VK_RWIN;
}

static void vk_display_name(UINT vk, wchar_t *out, size_t out_count) {
    UINT scan;
    LONG lparam;

    if (out_count == 0) {
        return;
    }

    out[0] = L'\0';

    if (vk >= 'A' && vk <= 'Z') {
        swprintf(out, out_count, L"%c", (wchar_t)vk);
        return;
    }
    if (vk >= '0' && vk <= '9') {
        swprintf(out, out_count, L"%c", (wchar_t)vk);
        return;
    }
    if (vk >= VK_F1 && vk <= VK_F24) {
        swprintf(out, out_count, L"F%u", (unsigned int)(vk - VK_F1 + 1));
        return;
    }
    if (vk == VK_SPACE) {
        wcsncpy(out, L"Space", out_count - 1);
        out[out_count - 1] = L'\0';
        return;
    }

    scan = MapVirtualKeyW(vk, MAPVK_VK_TO_VSC);
    lparam = (LONG)(scan << 16);
    if (GetKeyNameTextW(lparam, out, (int)out_count) <= 0) {
        swprintf(out, out_count, L"VK%u", (unsigned int)vk);
    }
}

static void format_hotkey_label(UINT modifiers, UINT vk, wchar_t *out, size_t out_count) {
    wchar_t key_name[64];
    int wrote = 0;

    if (out_count == 0) {
        return;
    }

    out[0] = L'\0';
    if (modifiers & MOD_CONTROL) {
        wrote += swprintf(out + wrote, out_count - (size_t)wrote, L"Ctrl + ");
    }
    if (modifiers & MOD_ALT) {
        wrote += swprintf(out + wrote, out_count - (size_t)wrote, L"Alt + ");
    }
    if (modifiers & MOD_SHIFT) {
        wrote += swprintf(out + wrote, out_count - (size_t)wrote, L"Shift + ");
    }
    if (modifiers & MOD_WIN) {
        wrote += swprintf(out + wrote, out_count - (size_t)wrote, L"Win + ");
    }

    vk_display_name(vk, key_name, 64);
    swprintf(out + wrote, out_count - (size_t)wrote, L"%ls", key_name);
    out[out_count - 1] = L'\0';
}

static const wchar_t *current_hotkey_label(void) {
    static wchar_t label[128];
    format_hotkey_label(g_app.hotkey_modifiers, g_app.hotkey_vk, label, 128);
    return label;
}

static void sync_hotkey_controls(void) {
    if (g_app.hotkey_check != NULL) {
        sync_switch_check(g_app.hotkey_check, g_app.hotkey_enabled);
    }
    if (g_app.hotkey_display != NULL) {
        set_hotkey_display_text(g_app.hotkey_capture ? L"请按下新的组合键..."
                                                     : current_hotkey_label());
        EnableWindow(g_app.hotkey_display, g_app.hotkey_enabled);
        redraw_control(g_app.hotkey_display);
    }
    if (g_app.hotkey_set_button != NULL) {
        EnableWindow(g_app.hotkey_set_button, g_app.hotkey_enabled);
        SetWindowTextW(g_app.hotkey_set_button,
                       g_app.hotkey_capture ? L"按键中..." : L"设置快捷键");
        redraw_control(g_app.hotkey_set_button);
    }
}

static void unregister_configured_hotkey(void) {
    if (g_app.hotkey_registered) {
        UnregisterHotKey(g_app.hwnd, HOTKEY_SHOW_WINDOW);
        g_app.hotkey_registered = 0;
    }
}

static int register_configured_hotkey(void) {
    unregister_configured_hotkey();
    if (!g_app.hotkey_enabled) {
        return 1;
    }

    if (g_app.hotkey_vk == 0 || is_modifier_vk(g_app.hotkey_vk)) {
        return 0;
    }

    g_app.hotkey_registered =
        RegisterHotKey(g_app.hwnd, HOTKEY_SHOW_WINDOW,
                       g_app.hotkey_modifiers, g_app.hotkey_vk) != FALSE;
    return g_app.hotkey_registered;
}

static int build_startup_command(wchar_t *out, size_t out_count) {
    wchar_t exe_path[MAX_PATH];

    if (out_count == 0) {
        return 0;
    }

    if (GetModuleFileNameW(NULL, exe_path, MAX_PATH) == 0) {
        out[0] = L'\0';
        return 0;
    }

    if (g_app.start_hidden) {
        swprintf(out, out_count, L"\"%ls\" --hidden", exe_path);
    } else {
        swprintf(out, out_count, L"\"%ls\"", exe_path);
    }
    return 1;
}

static int startup_is_enabled(void) {
    HKEY key;
    LONG result;
    wchar_t value[MAX_PATH * 2];
    DWORD value_size = sizeof(value);
    DWORD type = 0;

    result = RegOpenKeyExW(HKEY_CURRENT_USER, RUN_KEY_PATH, 0, KEY_READ, &key);
    if (result != ERROR_SUCCESS) {
        return 0;
    }

    result = RegQueryValueExW(key, RUN_VALUE_NAME, NULL, &type,
                              (LPBYTE)value, &value_size);
    RegCloseKey(key);

    return result == ERROR_SUCCESS && type == REG_SZ && value[0] != L'\0';
}

static int set_startup_enabled(int enabled) {
    HKEY key;
    LONG result;

    if (enabled) {
        wchar_t command[MAX_PATH * 2];
        DWORD bytes;

        if (!build_startup_command(command, MAX_PATH * 2)) {
            return 0;
        }

        result = RegCreateKeyExW(HKEY_CURRENT_USER, RUN_KEY_PATH, 0, NULL, 0,
                                 KEY_SET_VALUE, NULL, &key, NULL);
        if (result != ERROR_SUCCESS) {
            return 0;
        }

        bytes = (DWORD)((wcslen(command) + 1) * sizeof(wchar_t));
        result = RegSetValueExW(key, RUN_VALUE_NAME, 0, REG_SZ,
                                (const BYTE *)command, bytes);
        RegCloseKey(key);
        return result == ERROR_SUCCESS;
    }

    result = RegOpenKeyExW(HKEY_CURRENT_USER, RUN_KEY_PATH, 0, KEY_SET_VALUE, &key);
    if (result != ERROR_SUCCESS) {
        return result == ERROR_FILE_NOT_FOUND;
    }

    result = RegDeleteValueW(key, RUN_VALUE_NAME);
    RegCloseKey(key);
    return result == ERROR_SUCCESS || result == ERROR_FILE_NOT_FOUND;
}

static void sync_startup_checkbox(void) {
    if (g_app.startup_check != NULL) {
        sync_switch_check(g_app.startup_check, startup_is_enabled());
    }
}

static void handle_startup_toggle(void) {
    int enabled = !startup_is_enabled();

    if (!set_startup_enabled(enabled)) {
        MessageBoxW(g_app.hwnd,
                    enabled ? L"开机自启动设置失败，请检查系统权限。"
                            : L"取消开机自启动失败，请检查系统权限。",
                    APP_TITLE, MB_ICONWARNING);
        sync_startup_checkbox();
        return;
    }

    sync_startup_checkbox();
    set_status(enabled ? L"已开启开机自启动。" : L"已关闭开机自启动。");
}

static void handle_theme_toggle(void) {
    g_app.dark_theme = !g_app.dark_theme;
    refresh_theme_brushes();
    save_settings();
    apply_theme_to_controls();
    set_status(g_app.dark_theme ? L"已切换到夜间模式。" : L"已切换到白天模式。");
}

static void handle_start_hidden_toggle(void) {
    g_app.start_hidden = !g_app.start_hidden;
    save_settings();
    if (startup_is_enabled()) {
        set_startup_enabled(1);
    }
    sync_switch_check(g_app.start_hidden_check, g_app.start_hidden);
    set_status(g_app.start_hidden ? L"开机自启动时将隐藏到托盘；手动双击仍显示窗口。"
                                  : L"开机自启动时将显示主窗口。");
}

static void handle_filter_sensitive_toggle(void) {
    g_app.filter_sensitive = !g_app.filter_sensitive;
    save_settings();
    sync_switch_check(g_app.filter_sensitive_check, g_app.filter_sensitive);
    set_status(g_app.filter_sensitive ? L"已开启敏感内容过滤。"
                                      : L"已关闭敏感内容过滤。");
}

static void handle_hotkey_toggle(void) {
    g_app.hotkey_enabled = !g_app.hotkey_enabled;
    g_app.hotkey_capture = 0;

    if (g_app.hotkey_enabled && !register_configured_hotkey()) {
        wchar_t message[256];
        swprintf(message, 256, L"快捷键 %ls 注册失败，可能已被其他软件占用。",
                 current_hotkey_label());
        g_app.hotkey_enabled = 0;
        sync_hotkey_controls();
        save_settings();
        MessageBoxW(g_app.hwnd, message, APP_TITLE, MB_ICONWARNING);
        set_status(L"快捷键未启用。");
        return;
    }

    if (!g_app.hotkey_enabled) {
        unregister_configured_hotkey();
    }

    sync_hotkey_controls();
    save_settings();
    set_status(g_app.hotkey_enabled ? L"快捷键已启用。"
                                    : L"快捷键已关闭。");
}

static void begin_hotkey_capture(void) {
    if (!g_app.hotkey_enabled) {
        return;
    }

    g_app.hotkey_capture = 1;
    unregister_configured_hotkey();
    sync_hotkey_controls();
    SetFocus(g_app.hwnd);
    set_status(L"请按下新的快捷键组合，按 Esc 取消。");
}

static void cancel_hotkey_capture(void) {
    g_app.hotkey_capture = 0;
    register_configured_hotkey();
    sync_hotkey_controls();
    set_status(L"已取消快捷键设置。");
}

static void finish_hotkey_capture(UINT vk) {
    UINT modifiers = 0;

    if (GetKeyState(VK_CONTROL) & 0x8000) {
        modifiers |= MOD_CONTROL;
    }
    if (GetKeyState(VK_MENU) & 0x8000) {
        modifiers |= MOD_ALT;
    }
    if (GetKeyState(VK_SHIFT) & 0x8000) {
        modifiers |= MOD_SHIFT;
    }
    if ((GetKeyState(VK_LWIN) & 0x8000) || (GetKeyState(VK_RWIN) & 0x8000)) {
        modifiers |= MOD_WIN;
    }

    if (vk == VK_ESCAPE) {
        cancel_hotkey_capture();
        return;
    }

    if (is_modifier_vk(vk)) {
        return;
    }

    if (modifiers == 0) {
        set_status(L"快捷键需要包含 Ctrl、Alt、Shift 或 Win，并且还要有一个普通按键。");
        return;
    }

    g_app.hotkey_modifiers = modifiers;
    g_app.hotkey_vk = vk;
    g_app.hotkey_capture = 0;

    if (!register_configured_hotkey()) {
        wchar_t message[256];
        swprintf(message, 256, L"快捷键 %ls 注册失败，已自动关闭快捷键。",
                 current_hotkey_label());
        g_app.hotkey_enabled = 0;
        sync_hotkey_controls();
        save_settings();
        MessageBoxW(g_app.hwnd, message, APP_TITLE, MB_ICONWARNING);
        set_status(L"快捷键未启用。");
        return;
    }

    sync_hotkey_controls();
    save_settings();
    set_status(L"快捷键设置已保存并启用。");
}

static int command_line_has_hidden_flag(void) {
    const wchar_t *cmd = GetCommandLineW();
    return cmd != NULL && wcsstr(cmd, L"--hidden") != NULL;
}

static HFONT create_ui_font(int point_size, int weight) {
    HDC hdc = GetDC(NULL);
    int height = -MulDiv(point_size, GetDeviceCaps(hdc, LOGPIXELSY), 72);
    ReleaseDC(NULL, hdc);

    return CreateFontW(height, 0, 0, 0, weight, FALSE, FALSE, FALSE,
                       DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                       CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
                       L"Microsoft YaHei UI");
}

static HWND make_control(HWND parent, const wchar_t *class_name, const wchar_t *text,
                         DWORD style, DWORD ex_style, int id) {
    HWND control = CreateWindowExW(ex_style, class_name, text, style,
                                   0, 0, 0, 0, parent, (HMENU)(INT_PTR)id,
                                   GetModuleHandleW(NULL), NULL);
    if (control != NULL && g_app.font != NULL) {
        SendMessageW(control, WM_SETFONT, (WPARAM)g_app.font, TRUE);
    }
    return control;
}

static wchar_t *app_dup_wide(const wchar_t *text) {
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

static int add_tray_icon(HWND hwnd) {
    NOTIFYICONDATAW nid;

    ZeroMemory(&nid, sizeof(nid));
    nid.cbSize = sizeof(nid);
    nid.hWnd = hwnd;
    nid.uID = TRAY_UID;
    nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    nid.uCallbackMessage = WM_TRAYICON;
    nid.hIcon = LoadIconW(NULL, MAKEINTRESOURCEW(32512));
    wcsncpy(nid.szTip, L"ClipKeeper 正在监听剪贴板",
            sizeof(nid.szTip) / sizeof(nid.szTip[0]) - 1);
    nid.szTip[sizeof(nid.szTip) / sizeof(nid.szTip[0]) - 1] = L'\0';

    g_app.tray_added = Shell_NotifyIconW(NIM_ADD, &nid) != FALSE;
    return g_app.tray_added;
}

static void remove_tray_icon(HWND hwnd) {
    NOTIFYICONDATAW nid;

    if (!g_app.tray_added) {
        return;
    }

    ZeroMemory(&nid, sizeof(nid));
    nid.cbSize = sizeof(nid);
    nid.hWnd = hwnd;
    nid.uID = TRAY_UID;
    Shell_NotifyIconW(NIM_DELETE, &nid);
    g_app.tray_added = 0;
}

static void show_main_window(void) {
    ShowWindow(g_app.hwnd, SW_SHOWNORMAL);
    SetForegroundWindow(g_app.hwnd);
    update_status();
}

static void hide_main_window(void) {
    ShowWindow(g_app.hwnd, SW_HIDE);
}

static void toggle_pause_capture(void) {
    g_app.pause_capture = !g_app.pause_capture;
    sync_switch_check(g_app.pause_check, g_app.pause_capture);
    update_status();
}

static void show_tray_menu(void) {
    HMENU menu;
    POINT pt;
    int paused = g_app.pause_capture;

    menu = CreatePopupMenu();
    if (menu == NULL) {
        return;
    }

    AppendMenuW(menu, MF_STRING, ID_TRAY_SHOW, L"打开主窗口");
    AppendMenuW(menu, MF_STRING, ID_TRAY_QUICK_SEARCH, L"快速搜索");
    AppendMenuW(menu, MF_STRING, ID_TRAY_TOGGLE_PAUSE,
                paused ? L"继续监听" : L"暂停监听");
    AppendMenuW(menu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(menu, MF_STRING, ID_TRAY_EXIT, L"退出 ClipKeeper");

    GetCursorPos(&pt);
    SetForegroundWindow(g_app.hwnd);
    TrackPopupMenu(menu, TPM_RIGHTBUTTON | TPM_BOTTOMALIGN | TPM_LEFTALIGN,
                   pt.x, pt.y, 0, g_app.hwnd, NULL);
    DestroyMenu(menu);
}

static unsigned long list_selected_id(void) {
    LRESULT selected = SendMessageW(g_app.history_list, LB_GETCURSEL, 0, 0);
    if (selected == LB_ERR) {
        return 0;
    }
    return (unsigned long)(ULONG_PTR)SendMessageW(g_app.history_list, LB_GETITEMDATA,
                                                 (WPARAM)selected, 0);
}

static void update_action_buttons(void) {
    unsigned long id = list_selected_id();
    const ClipItem *item = store_find_const(&g_app.store, id);
    BOOL has_selection = id != 0;
    EnableWindow(g_app.copy_button, has_selection);
    EnableWindow(g_app.pin_button, has_selection);
    EnableWindow(g_app.delete_button, has_selection);
    EnableWindow(g_app.clear_button, g_app.store.count > 0);
    SetWindowTextW(g_app.pin_button, item != NULL && item->pinned ? L"取消置顶" : L"置顶");
}

static void update_status(void) {
    wchar_t text[256];
    int paused = g_app.pause_capture;
    swprintf(text, 256, L"已保存 %zu 条，当前显示 %zu 条 | %ls",
             g_app.store.count,
             g_app.search.count,
             paused ? L"已暂停监听" : L"正在监听剪贴板");
    set_status(text);
}

static void set_preview_text(const wchar_t *text) {
    const wchar_t *safe_text = text != NULL ? text : L"";

    if (g_app.preview_edit == NULL || !IsWindow(g_app.preview_edit)) {
        return;
    }

    SendMessageW(g_app.preview_edit, WM_SETREDRAW, FALSE, 0);
    SetWindowTextW(g_app.preview_edit, safe_text);
    SendMessageW(g_app.preview_edit, EM_SETSEL, 0, 0);
    SendMessageW(g_app.preview_edit, EM_SCROLLCARET, 0, 0);
    SendMessageW(g_app.preview_edit, WM_SETREDRAW, TRUE, 0);
    redraw_opaque_control(g_app.preview_edit);
}

static void update_preview(void) {
    unsigned long id = list_selected_id();
    const ClipItem *item = store_find_const(&g_app.store, id);

    if (item == NULL) {
        set_preview_text(L"请选择左侧记录查看完整内容。");
    } else {
        set_preview_text(item->text);
    }

    update_action_buttons();
}

static void select_list_item_by_id(unsigned long id) {
    LRESULT count;
    LRESULT i;

    if (id == 0) {
        SendMessageW(g_app.history_list, LB_SETCURSEL, (WPARAM)-1, 0);
        update_preview();
        return;
    }

    count = SendMessageW(g_app.history_list, LB_GETCOUNT, 0, 0);
    for (i = 0; i < count; i++) {
        unsigned long item_id = (unsigned long)(ULONG_PTR)SendMessageW(
            g_app.history_list, LB_GETITEMDATA, (WPARAM)i, 0);
        if (item_id == id) {
            SendMessageW(g_app.history_list, LB_SETCURSEL, (WPARAM)i, 0);
            update_preview();
            return;
        }
    }

    if (count > 0) {
        SendMessageW(g_app.history_list, LB_SETCURSEL, 0, 0);
    }
    update_preview();
}

static void refresh_history_list(unsigned long preferred_id) {
    wchar_t keyword[256];
    size_t i;

    GetWindowTextW(g_app.search_edit, keyword, 256);
    if (!store_search(&g_app.store, keyword, &g_app.search)) {
        set_status(L"搜索失败：内存不足。");
        return;
    }

    SendMessageW(g_app.history_list, WM_SETREDRAW, FALSE, 0);
    SendMessageW(g_app.history_list, LB_RESETCONTENT, 0, 0);

    for (i = 0; i < g_app.search.count; i++) {
        unsigned long id = g_app.search.ids[i];
        const ClipItem *item = store_find_const(&g_app.store, id);
        wchar_t preview[128];
        wchar_t created[32];
        wchar_t row[512];
        LRESULT index;

        if (item == NULL) {
            continue;
        }

        store_preview_text(item->text, preview, 128);
        store_format_time(item->created_at, created, 32);
        swprintf(row, 512, L"%ls  %ls  |  复制 %u 次  |  %ls",
                 item->pinned ? L"[置顶]" : L"      ",
                 created,
                 item->copy_count,
                 preview);

        index = SendMessageW(g_app.history_list, LB_ADDSTRING, 0, (LPARAM)row);
        if (index != LB_ERR && index != LB_ERRSPACE) {
            SendMessageW(g_app.history_list, LB_SETITEMDATA, (WPARAM)index,
                         (LPARAM)(ULONG_PTR)item->id);
        }
    }

    SendMessageW(g_app.history_list, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(g_app.history_list, NULL, TRUE);

    select_list_item_by_id(preferred_id);
    update_status();
}

static void save_store_or_warn(void) {
    if (!store_save(&g_app.store, g_app.data_path)) {
        MessageBoxW(g_app.hwnd, L"无法保存 clips.tsv。", APP_TITLE, MB_ICONWARNING);
    }
}

static size_t enforce_history_limit(int refresh) {
    size_t removed = store_prune_to_limit(&g_app.store, g_app.max_history);

    if (removed > 0) {
        save_store_or_warn();
        if (refresh) {
            refresh_history_list(list_selected_id());
        }
    }

    return removed;
}

static void handle_history_limit_change(void) {
    wchar_t text[32];
    wchar_t status[128];
    wchar_t *end = NULL;
    unsigned long parsed;
    size_t removed;

    if (g_app.max_history_edit == NULL) {
        return;
    }

    GetWindowTextW(g_app.max_history_edit, text, 32);
    parsed = wcstoul(text, &end, 10);
    if (end == text) {
        sync_history_settings_controls();
        set_status(L"保存上限未修改：请输入数字。");
        return;
    }

    g_app.max_history = clamp_history_limit((size_t)parsed);
    save_settings();
    sync_history_settings_controls();
    removed = enforce_history_limit(1);

    swprintf(status, 128, L"保存上限已设为 %u 条，本次清理 %u 条旧记录。",
             (unsigned int)g_app.max_history, (unsigned int)removed);
    set_status(status);
}

static void capture_clipboard_text(void) {
    wchar_t *text;
    ClipItem *item;

    text = clipboard_read_text(g_app.hwnd);
    if (text == NULL) {
        return;
    }

    if (g_app.last_written_text != NULL && wcscmp(text, g_app.last_written_text) == 0) {
        free(g_app.last_written_text);
        g_app.last_written_text = NULL;
        free(text);
        return;
    }

    if (g_app.filter_sensitive && looks_sensitive_clip_text(text)) {
        free(text);
        set_status(L"已跳过疑似敏感内容，未保存到历史。");
        return;
    }

    item = store_add_text(&g_app.store, text);
    free(text);

    if (item != NULL) {
        enforce_history_limit(0);
        save_store_or_warn();
        refresh_history_list(item->id);
    }
}

static void copy_selected_clip(void) {
    unsigned long id = list_selected_id();
    const ClipItem *item = store_find_const(&g_app.store, id);

    if (item == NULL) {
        return;
    }

    free(g_app.last_written_text);
    g_app.last_written_text = app_dup_wide(item->text);
    if (!clipboard_write_text(g_app.hwnd, item->text)) {
        free(g_app.last_written_text);
        g_app.last_written_text = NULL;
        MessageBoxW(g_app.hwnd, L"无法写入剪贴板。", APP_TITLE,
                    MB_ICONERROR);
        return;
    }

    store_increment_copy_count(&g_app.store, id);
    save_store_or_warn();
    refresh_history_list(id);
    set_status(L"已复制选中记录到剪贴板。");
}

static void toggle_pin_selected_clip(void) {
    unsigned long id = list_selected_id();
    if (id == 0) {
        return;
    }

    if (store_toggle_pin(&g_app.store, id)) {
        save_store_or_warn();
        refresh_history_list(id);
    }
}

static void delete_selected_clip(void) {
    unsigned long id = list_selected_id();
    if (id == 0) {
        return;
    }

    if (store_delete(&g_app.store, id)) {
        save_store_or_warn();
        refresh_history_list(0);
    }
}

static void clear_all_clips(void) {
    if (g_app.store.count == 0) {
        return;
    }

    if (MessageBoxW(g_app.hwnd, L"确定要清空所有剪贴板历史吗？", APP_TITLE,
                    MB_YESNO | MB_ICONQUESTION) != IDYES) {
        return;
    }

    store_clear(&g_app.store);
    save_store_or_warn();
    refresh_history_list(0);
}

static unsigned long quick_selected_id(void) {
    LRESULT selected;

    if (g_app.quick_list == NULL) {
        return 0;
    }

    selected = SendMessageW(g_app.quick_list, LB_GETCURSEL, 0, 0);
    if (selected == LB_ERR) {
        return 0;
    }

    return (unsigned long)(ULONG_PTR)SendMessageW(g_app.quick_list, LB_GETITEMDATA,
                                                 (WPARAM)selected, 0);
}

static void refresh_quick_search_list(unsigned long preferred_id) {
    wchar_t keyword[256];
    size_t i;
    LRESULT first_match = LB_ERR;

    if (g_app.quick_edit == NULL || g_app.quick_list == NULL) {
        return;
    }

    GetWindowTextW(g_app.quick_edit, keyword, 256);
    if (!store_search(&g_app.store, keyword, &g_app.quick_search)) {
        set_status(L"快速搜索失败：内存不足。");
        return;
    }

    SendMessageW(g_app.quick_list, WM_SETREDRAW, FALSE, 0);
    SendMessageW(g_app.quick_list, LB_RESETCONTENT, 0, 0);

    for (i = 0; i < g_app.quick_search.count; i++) {
        unsigned long id = g_app.quick_search.ids[i];
        const ClipItem *item = store_find_const(&g_app.store, id);
        wchar_t preview[128];
        wchar_t created[32];
        wchar_t row[512];
        LRESULT index;

        if (item == NULL) {
            continue;
        }

        store_preview_text(item->text, preview, 128);
        store_format_time(item->created_at, created, 32);
        swprintf(row, 512, L"%ls  %ls  |  复制 %u 次  |  %ls",
                 item->pinned ? L"[置顶]" : L"      ",
                 created,
                 item->copy_count,
                 preview);

        index = SendMessageW(g_app.quick_list, LB_ADDSTRING, 0, (LPARAM)row);
        if (index != LB_ERR && index != LB_ERRSPACE) {
            SendMessageW(g_app.quick_list, LB_SETITEMDATA, (WPARAM)index,
                         (LPARAM)(ULONG_PTR)item->id);
            if (id == preferred_id) {
                first_match = index;
            }
        }
    }

    SendMessageW(g_app.quick_list, WM_SETREDRAW, TRUE, 0);
    redraw_opaque_control(g_app.quick_list);

    if (first_match != LB_ERR) {
        SendMessageW(g_app.quick_list, LB_SETCURSEL, (WPARAM)first_match, 0);
    } else if (SendMessageW(g_app.quick_list, LB_GETCOUNT, 0, 0) > 0) {
        SendMessageW(g_app.quick_list, LB_SETCURSEL, 0, 0);
    }
}

static void copy_quick_selected_clip(void) {
    unsigned long id = quick_selected_id();
    const ClipItem *item = store_find_const(&g_app.store, id);

    if (item == NULL) {
        set_status(L"快速搜索没有选中记录。");
        return;
    }

    free(g_app.last_written_text);
    g_app.last_written_text = app_dup_wide(item->text);
    if (!clipboard_write_text(g_app.hwnd, item->text)) {
        free(g_app.last_written_text);
        g_app.last_written_text = NULL;
        MessageBoxW(g_app.hwnd, L"无法写入剪贴板。", APP_TITLE, MB_ICONERROR);
        return;
    }

    store_increment_copy_count(&g_app.store, id);
    save_store_or_warn();
    refresh_history_list(id);
    if (g_app.quick_hwnd != NULL) {
        ShowWindow(g_app.quick_hwnd, SW_HIDE);
    }
    set_status(L"已从快速搜索复制到剪贴板。");
}

static void position_quick_search_window(void) {
    RECT anchor;
    RECT work;
    int width = 640;
    int height = 360;
    int x;
    int y;

    if (g_app.quick_hwnd == NULL) {
        return;
    }

    if (IsWindowVisible(g_app.hwnd)) {
        GetWindowRect(g_app.hwnd, &anchor);
    } else {
        SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
        anchor = work;
    }

    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    x = anchor.left + ((anchor.right - anchor.left) - width) / 2;
    y = anchor.top + 96;
    if (x < work.left + 12) {
        x = work.left + 12;
    }
    if (x + width > work.right - 12) {
        x = work.right - width - 12;
    }
    if (y < work.top + 12) {
        y = work.top + 12;
    }
    if (y + height > work.bottom - 12) {
        y = work.bottom - height - 12;
    }

    SetWindowPos(g_app.quick_hwnd, HWND_TOPMOST, x, y, width, height,
                 SWP_NOACTIVATE);
}

static void show_quick_search(void) {
    if (g_app.quick_hwnd == NULL || !IsWindow(g_app.quick_hwnd)) {
        g_app.quick_hwnd = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST,
                                           QUICK_SEARCH_CLASS,
                                           L"快速搜索剪贴板",
                                           WS_POPUP | WS_CAPTION | WS_SYSMENU |
                                               WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
                                           CW_USEDEFAULT, CW_USEDEFAULT, 640, 360,
                                           g_app.hwnd, NULL,
                                           GetModuleHandleW(NULL), NULL);
        if (g_app.quick_hwnd == NULL) {
            MessageBoxW(g_app.hwnd, L"无法创建快速搜索窗口。", APP_TITLE,
                        MB_ICONERROR);
            return;
        }
    }

    position_quick_search_window();
    SetWindowTextW(g_app.quick_edit, L"");
    refresh_quick_search_list(0);
    ShowWindow(g_app.quick_hwnd, SW_SHOWNORMAL);
    SetForegroundWindow(g_app.quick_hwnd);
    SetFocus(g_app.quick_edit);
    SendMessageW(g_app.quick_edit, EM_SETSEL, 0, -1);
}

static LRESULT CALLBACK quick_search_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    switch (msg) {
    case WM_CREATE:
        g_app.quick_hwnd = hwnd;
        g_app.quick_edit = make_control(hwnd, L"EDIT", L"",
                                        WS_CHILD | WS_VISIBLE | WS_TABSTOP |
                                            ES_AUTOHSCROLL,
                                        WS_EX_CLIENTEDGE, IDC_QUICK_SEARCH_EDIT);
        g_app.quick_list = make_control(hwnd, L"LISTBOX", L"",
                                        WS_CHILD | WS_VISIBLE | WS_TABSTOP |
                                            WS_VSCROLL | LBS_NOTIFY |
                                            LBS_NOINTEGRALHEIGHT |
                                            LBS_HASSTRINGS,
                                        WS_EX_CLIENTEDGE, IDC_QUICK_SEARCH_LIST);
        SendMessageW(g_app.quick_edit, EM_SETCUEBANNER, FALSE,
                     (LPARAM)L"输入关键字，回车复制，Esc 关闭");
        SendMessageW(g_app.quick_list, LB_SETITEMHEIGHT, 0, 28);
        return 0;

    case WM_SIZE: {
        RECT rc;
        int width;
        int height;
        GetClientRect(hwnd, &rc);
        width = rc.right - rc.left;
        height = rc.bottom - rc.top;
        MoveWindow(g_app.quick_edit, 18, 18, width - 36, 34, TRUE);
        MoveWindow(g_app.quick_list, 18, 64, width - 36, height - 82, TRUE);
        return 0;
    }

    case WM_CLOSE:
        ShowWindow(hwnd, SW_HIDE);
        return 0;

    case WM_COMMAND:
        switch (LOWORD(wparam)) {
        case IDC_QUICK_SEARCH_EDIT:
            if (HIWORD(wparam) == EN_CHANGE) {
                refresh_quick_search_list(0);
            }
            return 0;

        case IDC_QUICK_SEARCH_LIST:
            if (HIWORD(wparam) == LBN_DBLCLK) {
                copy_quick_selected_clip();
            }
            return 0;

        default:
            break;
        }
        break;

    case WM_ERASEBKGND: {
        RECT rc;
        HDC hdc = (HDC)wparam;
        GetClientRect(hwnd, &rc);
        fill_solid_rect(hdc, &rc, theme_surface_color());
        return 1;
    }

    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX: {
        HDC hdc = (HDC)wparam;
        SetTextColor(hdc, theme_text_color());
        SetBkColor(hdc, theme_surface_color());
        SetBkMode(hdc, OPAQUE);
        return (LRESULT)g_app.surface_brush;
    }

    case WM_DESTROY:
        if (g_app.quick_hwnd == hwnd) {
            g_app.quick_hwnd = NULL;
            g_app.quick_edit = NULL;
            g_app.quick_list = NULL;
        }
        return 0;
    }

    return DefWindowProcW(hwnd, msg, wparam, lparam);
}

static void layout_controls(HWND hwnd) {
    RECT rc;
    int width;
    int height;
    int layout_width;
    int layout_left;
    int layout_right;
    int margin = 24;
    int gap = 16;
    int title_h = 34;
    int subtitle_h = 22;
    int label_h = 24;
    int edit_h = 32;
    int hotkey_h = 30;
    int settings_h = 30;
    int button_h = 38;
    int button_w = 132;
    int pause_w = 112;
    int hotkey_check_w = 112;
    int hotkey_button_w = 116;
    int filter_w = 136;
    int max_label_w = 72;
    int max_edit_w = 74;
    int quick_button_w = 116;
    int startup_w = 128;
    int hidden_w = 128;
    int theme_w = 116;
    int top_option_gap = 8;
    int top_options_w = startup_w + hidden_w + theme_w + top_option_gap * 2;
    int list_w;
    int content_top;
    int content_h;
    int preview_x;
    int preview_w;
    int button_y;
    int panel_pad = 18;
    int content_bottom;
    int status_w;
    int inner_left;
    int inner_right;
    int inner_w;
    int row1_y;
    int row2_y;
    int row3_y;
    int search_label_w = 92;
    int row2_right_w;
    int row2_display_w;
    int row3_x;

    GetClientRect(hwnd, &rc);
    width = rc.right - rc.left;
    height = rc.bottom - rc.top;
    layout_width = width > 1280 ? 1280 : width;
    layout_left = (width - layout_width) / 2;
    layout_right = layout_left + layout_width;

    g_app.hero_rect.left = layout_left + margin;
    g_app.hero_rect.top = 18;
    g_app.hero_rect.right = layout_right - margin;
    g_app.hero_rect.bottom = 114;

    g_app.search_rect.left = layout_left + margin;
    g_app.search_rect.top = g_app.hero_rect.bottom + 14;
    g_app.search_rect.right = layout_right - margin;
    g_app.search_rect.bottom = g_app.search_rect.top + 142;

    g_app.action_rect.left = layout_left + margin;
    g_app.action_rect.right = layout_right - margin;
    g_app.action_rect.bottom = height - 18;
    g_app.action_rect.top = g_app.action_rect.bottom - 66;

    content_top = g_app.search_rect.bottom + 16;
    content_bottom = g_app.action_rect.top - 16;
    content_h = content_bottom - content_top;
    if (content_h < 260) {
        content_h = 260;
    }

    list_w = (layout_width - margin * 2 - gap) / 2;
    preview_x = layout_left + margin + list_w + gap;
    preview_w = layout_right - preview_x - margin;

    g_app.left_panel_rect.left = layout_left + margin;
    g_app.left_panel_rect.top = content_top;
    g_app.left_panel_rect.right = layout_left + margin + list_w;
    g_app.left_panel_rect.bottom = content_top + content_h;

    g_app.right_panel_rect.left = preview_x;
    g_app.right_panel_rect.top = content_top;
    g_app.right_panel_rect.right = preview_x + preview_w;
    g_app.right_panel_rect.bottom = content_top + content_h;

    MoveWindow(g_app.title_text, g_app.hero_rect.left + panel_pad,
               g_app.hero_rect.top + 16,
               (g_app.hero_rect.right - g_app.hero_rect.left) - panel_pad * 2 -
                   top_options_w - gap,
               title_h, TRUE);
    MoveWindow(g_app.startup_check,
               g_app.hero_rect.right - panel_pad - top_options_w,
               g_app.hero_rect.top + 18,
               startup_w, title_h, TRUE);
    MoveWindow(g_app.start_hidden_check,
               g_app.hero_rect.right - panel_pad - top_options_w + startup_w + top_option_gap,
               g_app.hero_rect.top + 18,
               hidden_w, title_h, TRUE);
    MoveWindow(g_app.theme_check,
               g_app.hero_rect.right - panel_pad - theme_w,
               g_app.hero_rect.top + 18,
               theme_w, title_h, TRUE);
    MoveWindow(g_app.subtitle_text, g_app.hero_rect.left + panel_pad,
               g_app.hero_rect.top + 56,
               (g_app.hero_rect.right - g_app.hero_rect.left) - panel_pad * 2,
               subtitle_h, TRUE);

    inner_left = g_app.search_rect.left + panel_pad;
    inner_right = g_app.search_rect.right - panel_pad;
    inner_w = inner_right - inner_left;
    row1_y = g_app.search_rect.top + 16;
    row2_y = g_app.search_rect.top + 58;
    row3_y = g_app.search_rect.top + 100;

    MoveWindow(g_app.search_label, inner_left, row1_y + 4,
               search_label_w, label_h, TRUE);
    MoveWindow(g_app.pause_check, inner_right - pause_w, row1_y,
               pause_w, edit_h, TRUE);
    MoveWindow(g_app.search_edit, inner_left + search_label_w,
               row1_y,
               inner_w - search_label_w - pause_w - gap,
               edit_h, TRUE);

    row2_right_w = hotkey_button_w + gap + quick_button_w;
    row2_display_w = inner_right - row2_right_w - gap -
                     (inner_left + hotkey_check_w + gap);
    if (row2_display_w < 40) {
        row2_display_w = 40;
    }
    MoveWindow(g_app.hotkey_check, inner_left, row2_y + 1,
               hotkey_check_w, hotkey_h, TRUE);
    MoveWindow(g_app.hotkey_display, inner_left + hotkey_check_w + gap,
               row2_y,
               row2_display_w, hotkey_h, TRUE);
    MoveWindow(g_app.hotkey_set_button, inner_right - row2_right_w,
               row2_y - 1,
               hotkey_button_w, hotkey_h + 4, TRUE);
    MoveWindow(g_app.quick_search_button, inner_right - quick_button_w,
               row2_y - 1,
               quick_button_w, hotkey_h + 4, TRUE);

    row3_x = inner_left;
    MoveWindow(g_app.filter_sensitive_check, row3_x, row3_y,
               filter_w, settings_h, TRUE);
    row3_x += filter_w + 22;
    MoveWindow(g_app.max_history_label, row3_x,
               row3_y + 4,
               max_label_w, settings_h, TRUE);
    row3_x += max_label_w + 8;
    MoveWindow(g_app.max_history_edit, row3_x,
               row3_y - 2,
               max_edit_w, settings_h + 4, TRUE);

    MoveWindow(g_app.history_label, g_app.left_panel_rect.left + panel_pad,
               g_app.left_panel_rect.top + 14, list_w - panel_pad * 2, label_h, TRUE);
    MoveWindow(g_app.preview_label, g_app.right_panel_rect.left + panel_pad,
               g_app.right_panel_rect.top + 14, preview_w - panel_pad * 2, label_h, TRUE);
    MoveWindow(g_app.history_list, g_app.left_panel_rect.left + panel_pad,
               g_app.left_panel_rect.top + 48,
               list_w - panel_pad * 2,
               content_h - 64, TRUE);
    MoveWindow(g_app.preview_edit, g_app.right_panel_rect.left + panel_pad,
               g_app.right_panel_rect.top + 48,
               preview_w - panel_pad * 2,
               content_h - 64, TRUE);

    button_y = g_app.action_rect.top + 14;
    MoveWindow(g_app.copy_button, g_app.action_rect.left + panel_pad,
               button_y, button_w, button_h, TRUE);
    MoveWindow(g_app.pin_button, g_app.action_rect.left + panel_pad + (button_w + gap),
               button_y,
               button_w, button_h, TRUE);
    MoveWindow(g_app.delete_button, g_app.action_rect.left + panel_pad + (button_w + gap) * 2,
               button_y,
               button_w, button_h, TRUE);
    MoveWindow(g_app.clear_button, g_app.action_rect.left + panel_pad + (button_w + gap) * 3,
               button_y,
               button_w, button_h, TRUE);

    status_w = layout_width - margin * 2 - panel_pad * 2 - (button_w + gap) * 4;
    if (status_w < 220) {
        status_w = 220;
    }
    MoveWindow(g_app.status_text, g_app.action_rect.right - panel_pad - status_w,
               button_y + 8,
               status_w, 26, TRUE);
}

static void create_controls(HWND hwnd) {
    g_app.font = create_ui_font(10, FW_NORMAL);
    g_app.title_font = create_ui_font(18, FW_BOLD);
    g_app.small_font = create_ui_font(9, FW_NORMAL);

    g_app.title_text = make_control(hwnd, L"STATIC", L"ClipKeeper 剪贴板管家",
                                    WS_CHILD | WS_VISIBLE, 0, IDC_TITLE_TEXT);
    g_app.startup_check = make_control(hwnd, L"BUTTON", L"开机自启动",
                                       WS_CHILD | WS_VISIBLE | WS_TABSTOP |
                                           BS_CHECKBOX | BS_OWNERDRAW,
                                       0, IDC_STARTUP_CHECK);
    g_app.start_hidden_check = make_control(hwnd, L"BUTTON", L"启动时隐藏",
                                            WS_CHILD | WS_VISIBLE | WS_TABSTOP |
                                                BS_CHECKBOX | BS_OWNERDRAW,
                                            0, IDC_START_HIDDEN_CHECK);
    g_app.theme_check = make_control(hwnd, L"BUTTON", L"白天模式",
                                     WS_CHILD | WS_VISIBLE | WS_TABSTOP |
                                         BS_CHECKBOX | BS_OWNERDRAW,
                                     0, IDC_THEME_CHECK);
    g_app.subtitle_text = make_control(hwnd, L"STATIC",
                                       L"自动保存文本剪贴板，快速搜索、置顶，并一键复制回去。",
                                       WS_CHILD | WS_VISIBLE, 0, IDC_SUBTITLE_TEXT);
    SendMessageW(g_app.title_text, WM_SETFONT, (WPARAM)g_app.title_font, TRUE);
    SendMessageW(g_app.subtitle_text, WM_SETFONT, (WPARAM)g_app.small_font, TRUE);

    g_app.search_label = make_control(hwnd, L"STATIC", L"搜索历史",
                                      WS_CHILD | WS_VISIBLE, 0, IDC_SEARCH_LABEL);
    g_app.search_edit = make_control(hwnd, L"EDIT", L"",
                                     WS_CHILD | WS_VISIBLE | WS_TABSTOP |
                                         ES_AUTOHSCROLL,
                                     WS_EX_CLIENTEDGE, IDC_SEARCH_EDIT);
    SendMessageW(g_app.search_edit, EM_SETCUEBANNER, FALSE, (LPARAM)L"输入关键词过滤历史");

    g_app.pause_check = make_control(hwnd, L"BUTTON", L"暂停监听",
                                     WS_CHILD | WS_VISIBLE | WS_TABSTOP |
                                         BS_CHECKBOX | BS_OWNERDRAW,
                                     0, IDC_PAUSE_CHECK);
    g_app.hotkey_check = make_control(hwnd, L"BUTTON", L"启用快捷键",
                                      WS_CHILD | WS_VISIBLE | WS_TABSTOP |
                                          BS_CHECKBOX | BS_OWNERDRAW,
                                      0, IDC_HOTKEY_CHECK);
    g_app.hotkey_display = make_control(hwnd, L"EDIT", L"",
                                        WS_CHILD | WS_VISIBLE | ES_READONLY |
                                            ES_AUTOHSCROLL,
                                        WS_EX_CLIENTEDGE, IDC_HOTKEY_DISPLAY);
    g_app.hotkey_set_button = make_control(hwnd, L"BUTTON", L"设置快捷键",
                                           WS_CHILD | WS_VISIBLE | WS_TABSTOP |
                                               BS_OWNERDRAW,
                                           0, IDC_HOTKEY_SET_BUTTON);
    g_app.filter_sensitive_check = make_control(hwnd, L"BUTTON", L"跳过敏感内容",
                                                WS_CHILD | WS_VISIBLE | WS_TABSTOP |
                                                    BS_CHECKBOX | BS_OWNERDRAW,
                                                0, IDC_FILTER_SENSITIVE_CHECK);
    g_app.max_history_label = make_control(hwnd, L"STATIC", L"保存上限",
                                           WS_CHILD | WS_VISIBLE, 0,
                                           IDC_MAX_HISTORY_LABEL);
    g_app.max_history_edit = make_control(hwnd, L"EDIT", L"",
                                          WS_CHILD | WS_VISIBLE | WS_TABSTOP |
                                              ES_AUTOHSCROLL | ES_NUMBER,
                                          WS_EX_CLIENTEDGE, IDC_MAX_HISTORY_EDIT);
    g_app.quick_search_button = make_control(hwnd, L"BUTTON", L"快速搜索",
                                             WS_CHILD | WS_VISIBLE | WS_TABSTOP |
                                                 BS_OWNERDRAW,
                                             0, IDC_QUICK_SEARCH_BUTTON);
    g_app.history_label = make_control(hwnd, L"STATIC", L"历史记录",
                                       WS_CHILD | WS_VISIBLE, 0, IDC_HISTORY_LABEL);
    g_app.history_list = make_control(hwnd, L"LISTBOX", L"",
                                      WS_CHILD | WS_VISIBLE | WS_TABSTOP |
                                          WS_VSCROLL | LBS_NOTIFY |
                                          LBS_NOINTEGRALHEIGHT | LBS_OWNERDRAWFIXED |
                                          LBS_HASSTRINGS,
                                      0, IDC_HISTORY_LIST);
    SendMessageW(g_app.history_list, LB_SETHORIZONTALEXTENT, 900, 0);
    SendMessageW(g_app.history_list, LB_SETITEMHEIGHT, 0, 44);

    g_app.preview_label = make_control(hwnd, L"STATIC", L"内容预览",
                                       WS_CHILD | WS_VISIBLE, 0, IDC_PREVIEW_LABEL);
    g_app.preview_edit = make_control(hwnd, L"EDIT", L"请选择左侧记录查看完整内容。",
                                      WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_CLIPSIBLINGS |
                                          WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL |
                                          ES_READONLY,
                                      WS_EX_CLIENTEDGE, IDC_PREVIEW_EDIT);
    g_app.copy_button = make_control(hwnd, L"BUTTON", L"复制到剪贴板",
                                     WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
                                     0, IDC_COPY_BUTTON);
    g_app.pin_button = make_control(hwnd, L"BUTTON", L"置顶",
                                    WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
                                    0, IDC_PIN_BUTTON);
    g_app.delete_button = make_control(hwnd, L"BUTTON", L"删除",
                                       WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
                                       0, IDC_DELETE_BUTTON);
    g_app.clear_button = make_control(hwnd, L"BUTTON", L"清空全部",
                                      WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
                                      0, IDC_CLEAR_BUTTON);
    g_app.status_text = make_control(hwnd, L"STATIC", L"正在启动...",
                                     WS_CHILD | WS_VISIBLE | SS_LEFTNOWORDWRAP |
                                         SS_ENDELLIPSIS,
                                     0, IDC_STATUS_TEXT);
    SendMessageW(g_app.status_text, WM_SETFONT, (WPARAM)g_app.small_font, TRUE);
    sync_startup_checkbox();
    sync_hotkey_controls();
    sync_history_settings_controls();
    apply_theme_to_controls();
}

static void draw_owner_button(const DRAWITEMSTRUCT *draw) {
    wchar_t text[128];
    RECT rc = draw->rcItem;
    RECT bounds = draw->rcItem;
    COLORREF fill;
    COLORREF border;
    COLORREF text_color;
    int pressed = (draw->itemState & ODS_SELECTED) != 0;
    int disabled = (draw->itemState & ODS_DISABLED) != 0;
    int danger = draw->CtlID == IDC_DELETE_BUTTON || draw->CtlID == IDC_CLEAR_BUTTON;
    HGDIOBJ old_font;

    GetWindowTextW(draw->hwndItem, text, 128);
    fill_solid_rect(draw->hDC, &bounds, theme_surface_color());
    InflateRect(&rc, -2, -2);

    if (disabled) {
        fill = g_app.dark_theme ? RGB(47, 55, 70) : RGB(230, 235, 242);
        border = g_app.dark_theme ? RGB(65, 74, 92) : RGB(218, 226, 236);
        text_color = g_app.dark_theme ? RGB(130, 140, 154) : RGB(138, 149, 166);
    } else if (danger) {
        fill = pressed ? RGB(218, 82, 88) : RGB(255, 112, 118);
        border = RGB(255, 185, 188);
        text_color = RGB(255, 255, 255);
    } else {
        fill = g_app.dark_theme
                   ? (pressed ? RGB(65, 93, 138) : RGB(80, 118, 178))
                   : (pressed ? RGB(34, 111, 220) : RGB(0, 122, 255));
        border = g_app.dark_theme ? RGB(132, 162, 211) : RGB(184, 218, 255);
        text_color = RGB(255, 255, 255);
    }

    draw_round_rect(draw->hDC, &rc, 24, fill, border, 1);
    old_font = SelectObject(draw->hDC, g_app.font);
    SetBkMode(draw->hDC, TRANSPARENT);
    SetTextColor(draw->hDC, text_color);
    DrawTextW(draw->hDC, text, -1, &rc,
              DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    SelectObject(draw->hDC, old_font);
}

static int switch_checked_for_control(UINT control_id) {
    switch (control_id) {
    case IDC_STARTUP_CHECK:
        return startup_is_enabled();
    case IDC_START_HIDDEN_CHECK:
        return g_app.start_hidden;
    case IDC_THEME_CHECK:
        return g_app.dark_theme;
    case IDC_PAUSE_CHECK:
        return g_app.pause_capture;
    case IDC_FILTER_SENSITIVE_CHECK:
        return g_app.filter_sensitive;
    case IDC_HOTKEY_CHECK:
        return g_app.hotkey_enabled;
    default:
        return 0;
    }
}

static void draw_owner_switch(const DRAWITEMSTRUCT *draw) {
    wchar_t text[128];
    RECT rc = draw->rcItem;
    RECT track;
    RECT knob;
    RECT label;
    int checked = switch_checked_for_control(draw->CtlID);
    int disabled = (draw->itemState & ODS_DISABLED) != 0;
    COLORREF track_fill;
    COLORREF track_border;
    COLORREF knob_fill;
    COLORREF text_color;
    HGDIOBJ old_font;

    GetWindowTextW(draw->hwndItem, text, 128);
    fill_solid_rect(draw->hDC, &rc, theme_surface_color());

    track.left = rc.left + 2;
    track.top = rc.top + 5;
    track.right = track.left + 44;
    track.bottom = track.top + 22;

    if (disabled) {
        track_fill = g_app.dark_theme ? RGB(45, 52, 65) : RGB(224, 230, 238);
        track_border = g_app.dark_theme ? RGB(60, 70, 88) : RGB(210, 218, 230);
        knob_fill = g_app.dark_theme ? RGB(112, 122, 138) : RGB(246, 248, 252);
        text_color = g_app.dark_theme ? RGB(120, 130, 145) : RGB(142, 153, 170);
    } else if (checked) {
        track_fill = g_app.dark_theme ? RGB(64, 116, 207) : RGB(0, 122, 255);
        track_border = g_app.dark_theme ? RGB(126, 166, 235) : RGB(172, 216, 255);
        knob_fill = RGB(255, 255, 255);
        text_color = theme_title_text_color();
    } else {
        track_fill = g_app.dark_theme ? RGB(52, 63, 82) : RGB(229, 236, 246);
        track_border = g_app.dark_theme ? RGB(76, 91, 116) : RGB(210, 222, 238);
        knob_fill = g_app.dark_theme ? RGB(206, 216, 232) : RGB(255, 255, 255);
        text_color = theme_text_color();
    }

    draw_round_rect(draw->hDC, &track, 22, track_fill, track_border, 1);

    knob.top = track.top + 3;
    knob.bottom = track.bottom - 3;
    if (checked) {
        knob.right = track.right - 3;
        knob.left = knob.right - (knob.bottom - knob.top);
    } else {
        knob.left = track.left + 3;
        knob.right = knob.left + (knob.bottom - knob.top);
    }
    draw_round_rect(draw->hDC, &knob, 18, knob_fill, knob_fill, 1);

    label = rc;
    label.left = track.right + 8;
    label.right -= 2;
    old_font = SelectObject(draw->hDC, g_app.font);
    SetBkMode(draw->hDC, TRANSPARENT);
    SetTextColor(draw->hDC, text_color);
    DrawTextW(draw->hDC, text, -1, &label,
              DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    SelectObject(draw->hDC, old_font);
}

static void draw_owner_listbox(const DRAWITEMSTRUCT *draw) {
    wchar_t text[512];
    RECT rc = draw->rcItem;
    RECT pill = rc;
    int selected;
    HGDIOBJ old_font;

    if (draw->itemID == (UINT)-1) {
        fill_solid_rect(draw->hDC, &rc, theme_surface_color());
        return;
    }

    SendMessageW(draw->hwndItem, LB_GETTEXT, draw->itemID, (LPARAM)text);
    selected = (draw->itemState & ODS_SELECTED) != 0;
    fill_solid_rect(draw->hDC, &rc, theme_surface_color());

    InflateRect(&pill, -4, -4);
    if (selected) {
        draw_round_rect(draw->hDC, &pill, 18,
                        g_app.dark_theme ? RGB(63, 82, 121) : RGB(226, 241, 255),
                        g_app.dark_theme ? RGB(104, 128, 171) : RGB(198, 224, 255),
                        1);
    }

    old_font = SelectObject(draw->hDC, g_app.font);
    SetBkMode(draw->hDC, TRANSPARENT);
    SetTextColor(draw->hDC, selected ? theme_title_text_color() : theme_text_color());
    rc.left += 14;
    rc.right -= 10;
    DrawTextW(draw->hDC, text, -1, &rc,
              DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    SelectObject(draw->hDC, old_font);
}

static LRESULT CALLBACK window_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    switch (msg) {
    case WM_CREATE:
        g_app.hwnd = hwnd;
        store_init(&g_app.store);
        search_result_init(&g_app.search);
        search_result_init(&g_app.quick_search);
        build_app_paths();
        load_settings();
        refresh_theme_brushes();
        create_controls(hwnd);
        layout_controls(hwnd);

        if (!store_load(&g_app.store, g_app.data_path)) {
            MessageBoxW(hwnd, L"无法读取 clips.tsv，将使用空历史记录启动。",
                        APP_TITLE, MB_ICONWARNING);
        }
        enforce_history_limit(0);

        AddClipboardFormatListener(hwnd);
        add_tray_icon(hwnd);
        if (g_app.hotkey_enabled && !register_configured_hotkey()) {
            g_app.hotkey_enabled = 0;
            sync_hotkey_controls();
            save_settings();
            set_status(L"已跳过快捷键注册：当前组合键被占用。");
        }
        refresh_history_list(0);
        capture_clipboard_text();
        return 0;

    case WM_SIZE:
        if (wparam == SIZE_MINIMIZED) {
            hide_main_window();
            return 0;
        }
        layout_controls(hwnd);
        RedrawWindow(hwnd, NULL, NULL,
                     RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_UPDATENOW);
        return 0;

    case WM_CLOSE:
        if (g_app.allow_exit) {
            DestroyWindow(hwnd);
        } else {
            hide_main_window();
            set_status(g_app.hotkey_enabled
                           ? L"已隐藏到托盘，双击托盘图标或使用已设置的快捷键可重新打开。"
                           : L"已隐藏到托盘，双击托盘图标可重新打开。");
        }
        return 0;

    case WM_ERASEBKGND: {
        return 1;
    }

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        draw_window_chrome(hdc);
        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_DRAWITEM: {
        const DRAWITEMSTRUCT *draw = (const DRAWITEMSTRUCT *)lparam;
        if (draw->CtlID == IDC_HISTORY_LIST) {
            draw_owner_listbox(draw);
            return TRUE;
        }
        if (draw->CtlID == IDC_COPY_BUTTON || draw->CtlID == IDC_PIN_BUTTON ||
            draw->CtlID == IDC_DELETE_BUTTON || draw->CtlID == IDC_CLEAR_BUTTON ||
            draw->CtlID == IDC_HOTKEY_SET_BUTTON ||
            draw->CtlID == IDC_QUICK_SEARCH_BUTTON) {
            draw_owner_button(draw);
            return TRUE;
        }
        if (draw->CtlID == IDC_STARTUP_CHECK || draw->CtlID == IDC_START_HIDDEN_CHECK ||
            draw->CtlID == IDC_THEME_CHECK || draw->CtlID == IDC_PAUSE_CHECK ||
            draw->CtlID == IDC_HOTKEY_CHECK ||
            draw->CtlID == IDC_FILTER_SENSITIVE_CHECK) {
            draw_owner_switch(draw);
            return TRUE;
        }
        break;
    }

    case WM_CTLCOLORSTATIC: {
        HDC hdc = (HDC)wparam;
        HWND control = (HWND)lparam;
        if (control == g_app.preview_edit || control == g_app.hotkey_display) {
            SetBkMode(hdc, OPAQUE);
            SetBkColor(hdc, theme_surface_color());
            SetTextColor(hdc, theme_text_color());
            return (LRESULT)g_app.surface_brush;
        } else if (control == g_app.title_text) {
            SetBkMode(hdc, OPAQUE);
            SetBkColor(hdc, theme_surface_color());
            SetTextColor(hdc, theme_title_text_color());
        } else if (control == g_app.subtitle_text || control == g_app.status_text) {
            SetTextColor(hdc, theme_muted_text_color());
            SetBkMode(hdc, OPAQUE);
            SetBkColor(hdc, theme_surface_color());
        } else {
            SetBkMode(hdc, OPAQUE);
            SetBkColor(hdc, theme_surface_color());
            SetTextColor(hdc, theme_text_color());
        }
        return (LRESULT)g_app.surface_brush;
    }

    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX: {
        HDC hdc = (HDC)wparam;
        SetTextColor(hdc, theme_text_color());
        SetBkColor(hdc, theme_surface_color());
        SetBkMode(hdc, OPAQUE);
        return (LRESULT)g_app.surface_brush;
    }

    case WM_CTLCOLORBTN: {
        HDC hdc = (HDC)wparam;
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, theme_text_color());
        return (LRESULT)GetStockObject(HOLLOW_BRUSH);
    }

    case WM_GETMINMAXINFO: {
        MINMAXINFO *info = (MINMAXINFO *)lparam;
        info->ptMinTrackSize.x = 940;
        info->ptMinTrackSize.y = 620;
        return 0;
    }

    case WM_CLIPBOARDUPDATE:
        if (!g_app.pause_capture) {
            capture_clipboard_text();
        }
        return 0;

    case WM_HOTKEY:
        if (wparam == HOTKEY_SHOW_WINDOW) {
            show_quick_search();
        }
        return 0;

    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
        if (g_app.hotkey_capture) {
            finish_hotkey_capture((UINT)wparam);
            return 0;
        }
        break;

    case WM_TRAYICON:
        if (lparam == WM_LBUTTONDBLCLK || lparam == WM_LBUTTONUP) {
            show_main_window();
        } else if (lparam == WM_RBUTTONUP || lparam == WM_CONTEXTMENU) {
            show_tray_menu();
        }
        return 0;

    case WM_COMMAND:
        switch (LOWORD(wparam)) {
        case ID_TRAY_SHOW:
            show_main_window();
            return 0;

        case ID_TRAY_QUICK_SEARCH:
            show_quick_search();
            return 0;

        case ID_TRAY_TOGGLE_PAUSE:
            toggle_pause_capture();
            return 0;

        case ID_TRAY_EXIT:
            g_app.allow_exit = 1;
            DestroyWindow(hwnd);
            return 0;

        case IDC_SEARCH_EDIT:
            if (HIWORD(wparam) == EN_CHANGE) {
                refresh_history_list(list_selected_id());
            }
            return 0;

        case IDC_HISTORY_LIST:
            if (HIWORD(wparam) == LBN_SELCHANGE || HIWORD(wparam) == LBN_DBLCLK) {
                update_preview();
                if (HIWORD(wparam) == LBN_DBLCLK) {
                    copy_selected_clip();
                }
            }
            return 0;

        case IDC_COPY_BUTTON:
            copy_selected_clip();
            return 0;

        case IDC_PIN_BUTTON:
            toggle_pin_selected_clip();
            return 0;

        case IDC_DELETE_BUTTON:
            delete_selected_clip();
            return 0;

        case IDC_CLEAR_BUTTON:
            clear_all_clips();
            return 0;

        case IDC_PAUSE_CHECK:
            toggle_pause_capture();
            return 0;

        case IDC_STARTUP_CHECK:
            handle_startup_toggle();
            return 0;

        case IDC_THEME_CHECK:
            handle_theme_toggle();
            return 0;

        case IDC_START_HIDDEN_CHECK:
            handle_start_hidden_toggle();
            return 0;

        case IDC_HOTKEY_CHECK:
            handle_hotkey_toggle();
            return 0;

        case IDC_HOTKEY_SET_BUTTON:
            begin_hotkey_capture();
            return 0;

        case IDC_FILTER_SENSITIVE_CHECK:
            handle_filter_sensitive_toggle();
            return 0;

        case IDC_MAX_HISTORY_EDIT:
            if (HIWORD(wparam) == EN_KILLFOCUS) {
                handle_history_limit_change();
            }
            return 0;

        case IDC_QUICK_SEARCH_BUTTON:
            show_quick_search();
            return 0;

        default:
            break;
        }
        break;

    case WM_DESTROY:
        RemoveClipboardFormatListener(hwnd);
        if (g_app.hotkey_registered) {
            UnregisterHotKey(hwnd, HOTKEY_SHOW_WINDOW);
        }
        remove_tray_icon(hwnd);
        store_save(&g_app.store, g_app.data_path);
        free(g_app.last_written_text);
        search_result_free(&g_app.search);
        search_result_free(&g_app.quick_search);
        store_free(&g_app.store);
        DeleteObject(g_app.font);
        DeleteObject(g_app.title_font);
        DeleteObject(g_app.small_font);
        DeleteObject(g_app.background_brush);
        DeleteObject(g_app.surface_brush);
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProcW(hwnd, msg, wparam, lparam);
}

int WINAPI WinMain(HINSTANCE instance, HINSTANCE previous, LPSTR command_line, int show_command) {
    WNDCLASSW wc;
    WNDCLASSW quick_wc;
    HWND hwnd;
    MSG msg;

    (void)previous;
    (void)command_line;

    ZeroMemory(&g_app, sizeof(g_app));
    ZeroMemory(&wc, sizeof(wc));
    ZeroMemory(&quick_wc, sizeof(quick_wc));
    g_app.launched_hidden = command_line_has_hidden_flag();

    wc.lpfnWndProc = window_proc;
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.hInstance = instance;
    wc.lpszClassName = L"ClipKeeperWindowClass";
    wc.hCursor = LoadCursorW(NULL, MAKEINTRESOURCEW(32512));
    wc.hIcon = LoadIconW(NULL, MAKEINTRESOURCEW(32512));
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);

    if (!RegisterClassW(&wc)) {
        MessageBoxW(NULL, L"无法注册窗口类。", APP_TITLE, MB_ICONERROR);
        return 1;
    }

    quick_wc.lpfnWndProc = quick_search_proc;
    quick_wc.style = CS_HREDRAW | CS_VREDRAW;
    quick_wc.hInstance = instance;
    quick_wc.lpszClassName = QUICK_SEARCH_CLASS;
    quick_wc.hCursor = LoadCursorW(NULL, MAKEINTRESOURCEW(32512));
    quick_wc.hIcon = LoadIconW(NULL, MAKEINTRESOURCEW(32512));
    quick_wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    if (!RegisterClassW(&quick_wc)) {
        MessageBoxW(NULL, L"无法注册快速搜索窗口类。", APP_TITLE, MB_ICONERROR);
        return 1;
    }

    hwnd = CreateWindowExW(0, wc.lpszClassName, APP_TITLE,
                           WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
                           CW_USEDEFAULT, CW_USEDEFAULT, 980, 640,
                           NULL, NULL, instance, NULL);
    if (hwnd == NULL) {
        MessageBoxW(NULL, L"无法创建主窗口。", APP_TITLE, MB_ICONERROR);
        return 1;
    }

    if (g_app.launched_hidden && g_app.start_hidden) {
        ShowWindow(hwnd, SW_HIDE);
    } else {
        ShowWindow(hwnd, show_command);
        UpdateWindow(hwnd);
    }

    while (GetMessageW(&msg, NULL, 0, 0) > 0) {
        if (g_app.hotkey_capture &&
            (msg.message == WM_KEYDOWN || msg.message == WM_SYSKEYDOWN)) {
            finish_hotkey_capture((UINT)msg.wParam);
            continue;
        }
        if (g_app.quick_hwnd != NULL && IsWindowVisible(g_app.quick_hwnd) &&
            (msg.hwnd == g_app.quick_hwnd || IsChild(g_app.quick_hwnd, msg.hwnd)) &&
            msg.message == WM_KEYDOWN) {
            if (msg.wParam == VK_ESCAPE) {
                ShowWindow(g_app.quick_hwnd, SW_HIDE);
                continue;
            }
            if (msg.wParam == VK_RETURN) {
                copy_quick_selected_clip();
                continue;
            }
        }
        if (g_app.quick_hwnd != NULL && IsWindowVisible(g_app.quick_hwnd) &&
            IsDialogMessageW(g_app.quick_hwnd, &msg)) {
            continue;
        }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    return (int)msg.wParam;
}
