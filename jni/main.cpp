/**
 * libnametag.so — AML mod untuk SA-MP Mobile (Android, armeabi-v7a)
 *
 * Fitur:
 *   - Tampilkan nama di posisi tengah-kanan layar setiap frame
 *   - Baca nama dari name.txt (dicari di beberapa path otomatis)
 *   - Auto muncul kembali setiap relog / respawn — karena hook CHud::Draw
 *   - Background thread re-read name.txt setiap 3 detik (hot-reload)
 *
 * Build: ndk-build NDK_PROJECT_PATH=. APP_BUILD_SCRIPT=./jni/Android.mk
 * Output: libs/armeabi-v7a/libnametag.so
 *
 * Offsets target: libGTASA.so (verifikasi dengan readelf jika beda build)
 */

#include <jni.h>
#include <android/log.h>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cstdint>
#include <unistd.h>
#include <pthread.h>
#include "dobby.h"

// ─── Logging ─────────────────────────────────────────────────────────────────
#define LOG_TAG "libnametag"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

// ─── GTA internal "wide" char (UCS-2 LE) ─────────────────────────────────────
typedef unsigned short gta_wchar;

// ─── Offset dari base libGTASA.so (Thumb, bit-0 = 1) ─────────────────────────
// Verifikasi: readelf -s libGTASA.so | grep -E "PrintString|SetColor|CHud"
#define OFF_PrintString       0x5AA191u   // CFont::PrintString(float,float,gta_wchar*)
#define OFF_SetColor          0x5A8C11u   // CFont::SetColor(u8,u8,u8,u8)
#define OFF_SetScale          0x5A8B91u   // CFont::SetScale(float,float)
#define OFF_SetOrientation    0x5A8BD9u   // CFont::SetOrientation(int)  0=LEFT,1=CENTER,2=RIGHT
#define OFF_SetBackground     0x5A8C31u   // CFont::SetBackground(bool,bool)
#define OFF_SetDropShadow     0x5A8C51u   // CFont::SetDropShadow(int)
#define OFF_SetFontStyle      0x5A8B51u   // CFont::SetFontStyle(int)    0-3
#define OFF_SetEdge           0x5A8C71u   // CFont::SetEdge(int)
#define OFF_HudDraw           0x58E919u   // CHud::Draw()

// ─── Tipe pointer fungsi CFont (semua static/global state) ───────────────────
typedef void (*fn_PrintString)   (float x, float y, const gta_wchar* str);
typedef void (*fn_SetColor)      (unsigned char r, unsigned char g,
                                  unsigned char b, unsigned char a);
typedef void (*fn_SetScale)      (float w, float h);
typedef void (*fn_SetOrientation)(int orient);
typedef void (*fn_SetBackground) (bool enable, bool fit);
typedef void (*fn_SetDropShadow) (int size);
typedef void (*fn_SetFontStyle)  (int style);
typedef void (*fn_SetEdge)       (int size);
typedef void (*fn_HudDraw)       ();

static fn_PrintString    gPrintString    = nullptr;
static fn_SetColor       gSetColor       = nullptr;
static fn_SetScale       gSetScale       = nullptr;
static fn_SetOrientation gSetOrientation = nullptr;
static fn_SetBackground  gSetBackground  = nullptr;
static fn_SetDropShadow  gSetDropShadow  = nullptr;
static fn_SetFontStyle   gSetFontStyle   = nullptr;
static fn_SetEdge        gSetEdge        = nullptr;
static fn_HudDraw        gOrigHudDraw    = nullptr;

// ─── State nama ───────────────────────────────────────────────────────────────
static char       g_name_utf8[256] = "PlayerName";
static gta_wchar  g_name_wide[256] = {};
static bool       g_ready          = false;
static pthread_mutex_t g_mutex     = PTHREAD_MUTEX_INITIALIZER;

// ─── Path pencarian name.txt (urutan prioritas) ───────────────────────────────
// AML akan load .so dari mods/, tapi name.txt bisa kamu taruh di mana saja.
static const char* kNamePaths[] = {
    // ── Root SD card ──
    "/storage/emulated/0/name.txt",
    "/sdcard/name.txt",
    // ── Folder game SA-MP mobile (berbagai package name) ──
    "/storage/emulated/0/Android/data/com.sampmobilerp.game/mods/name.txt",
    "/storage/emulated/0/Android/data/com.rockstargames.gtasa/files/name.txt",
    "/storage/emulated/0/Android/data/com.rockstargames.gtasa/name.txt",
    // ── Folder SAMP ──
    "/storage/emulated/0/SAMP/name.txt",
    "/storage/emulated/0/Android/data/com.sampmobilerp.game/name.txt",
    // ── Folder mods ──
    "/storage/emulated/0/Android/data/com.sampmobilerp.game/mods/name.txt",
    nullptr
};

// ─── Konversi ASCII/Latin-1 → GTA wide char ──────────────────────────────────
static void to_wide(const char* src, gta_wchar* dst, int max_len) {
    int i = 0;
    while (*src && i < max_len - 1)
        dst[i++] = (gta_wchar)(unsigned char)*src++;
    dst[i] = 0;
}

// ─── Baca name.txt ────────────────────────────────────────────────────────────
static bool read_name_file() {
    for (int i = 0; kNamePaths[i]; ++i) {
        FILE* f = fopen(kNamePaths[i], "r");
        if (!f) continue;

        char buf[256] = {};
        bool ok = (fgets(buf, sizeof(buf), f) != nullptr);
        fclose(f);

        if (!ok) continue;

        // strip newline / carriage return
        int len = (int)strlen(buf);
        while (len > 0 && (buf[len-1] == '\n' || buf[len-1] == '\r'))
            buf[--len] = 0;

        if (len == 0) continue;

        pthread_mutex_lock(&g_mutex);
        strncpy(g_name_utf8, buf, sizeof(g_name_utf8) - 1);
        to_wide(g_name_utf8, g_name_wide, 256);
        pthread_mutex_unlock(&g_mutex);

        LOGI("[nametag] Loaded name '%s' from %s", g_name_utf8, kNamePaths[i]);
        return true;
    }
    LOGI("[nametag] name.txt not found — using default '%s'", g_name_utf8);
    pthread_mutex_lock(&g_mutex);
    to_wide(g_name_utf8, g_name_wide, 256);
    pthread_mutex_unlock(&g_mutex);
    return false;
}

// ─── Background thread: re-read name.txt setiap 3 detik (hot-reload) ─────────
static void* name_watcher(void* /*arg*/) {
    while (true) {
        sleep(3);
        read_name_file();
    }
    return nullptr;
}

// ─── Render nametag ───────────────────────────────────────────────────────────
// Dipanggil dari hook CHud::Draw — setiap frame.
//
// Koordinat GTA SA Virtual Screen: 640 × 448
//   x = 640 * 0.56 ≈ 358  → tengah, agak geser kanan
//   y = 448 * 0.10 ≈  45  → dekat atas (ubah sesuai selera)
//
static void render_nametag() {
    if (!gPrintString || !gSetColor || !gSetScale) return;

    pthread_mutex_lock(&g_mutex);
    gta_wchar name_copy[256];
    memcpy(name_copy, g_name_wide, sizeof(g_name_wide));
    pthread_mutex_unlock(&g_mutex);

    // ── posisi ────────────────────────────────────────────────────────────────
    // Tengah layar = 320, kita geser kanan sedikit → 360
    // Vertikal: 45  (dekat atas HUD); ganti ke 200 kalau mau di tengah layar
    const float POS_X = 360.0f;
    const float POS_Y = 45.0f;

    // ── font style: 1 = font GTA besar ───────────────────────────────────────
    if (gSetFontStyle)   gSetFontStyle(1);
    if (gSetBackground)  gSetBackground(false, false);

    // — Drop shadow (hitam, offset +1,+1) ─────────────────────────────────────
    if (gSetDropShadow) gSetDropShadow(0);
    if (gSetEdge)       gSetEdge(1);
    gSetColor(0, 0, 0, 200);
    gSetScale(0.50f, 1.0f);
    if (gSetOrientation) gSetOrientation(0);   // LEFT align
    gPrintString(POS_X + 1.5f, POS_Y + 1.5f, name_copy);

    // — Teks utama (putih) ────────────────────────────────────────────────────
    if (gSetEdge) gSetEdge(0);
    gSetColor(255, 255, 255, 255);
    gSetScale(0.50f, 1.0f);
    if (gSetOrientation) gSetOrientation(0);
    gPrintString(POS_X, POS_Y, name_copy);
}

// ─── Hook: CHud::Draw ─────────────────────────────────────────────────────────
static void hooked_HudDraw() {
    // Panggil fungsi asli dulu
    gOrigHudDraw();
    // Lalu render nametag kita di atasnya
    if (g_ready) render_nametag();
}

// ─── Baca base address libGTASA.so dari /proc/self/maps ──────────────────────
static uintptr_t get_lib_base(const char* lib_name) {
    char path[64];
    snprintf(path, sizeof(path), "/proc/%d/maps", getpid());
    FILE* f = fopen(path, "r");
    if (!f) return 0;

    char line[512];
    uintptr_t base = 0;
    while (fgets(line, sizeof(line), f)) {
        if (strstr(line, lib_name) && strstr(line, "r-xp")) {
            base = (uintptr_t)strtoul(line, nullptr, 16);
            break;
        }
    }
    fclose(f);
    return base;
}

// ─── Thumb address helper (set bit 0) ────────────────────────────────────────
static inline void* T(uintptr_t addr) { return (void*)(addr | 1u); }
// Raw address (clear bit 0, for calling)
static inline void* R(uintptr_t addr) { return (void*)(addr & ~1u); }

// ─── Constructor — dijalankan saat AML dlopen() .so ini ──────────────────────
__attribute__((constructor))
static void mod_init() {
    LOGI("[nametag] ===== libnametag init =====");

    // 1. Baca nama dari name.txt
    read_name_file();

    // 2. Cari base libGTASA.so
    uintptr_t base = get_lib_base("libGTASA.so");
    if (!base) {
        LOGE("[nametag] libGTASA.so not found in /proc/self/maps");
        return;
    }
    LOGI("[nametag] libGTASA.so base = 0x%08X", (unsigned)base);

    // 3. Resolve pointer CFont (di-call langsung, tanpa hook)
    gSetColor       = (fn_SetColor)      R(base + (OFF_SetColor       & ~1u));
    gSetScale       = (fn_SetScale)      R(base + (OFF_SetScale       & ~1u));
    gSetOrientation = (fn_SetOrientation)R(base + (OFF_SetOrientation & ~1u));
    gSetBackground  = (fn_SetBackground) R(base + (OFF_SetBackground  & ~1u));
    gSetDropShadow  = (fn_SetDropShadow) R(base + (OFF_SetDropShadow  & ~1u));
    gSetFontStyle   = (fn_SetFontStyle)  R(base + (OFF_SetFontStyle   & ~1u));
    gSetEdge        = (fn_SetEdge)       R(base + (OFF_SetEdge        & ~1u));
    gPrintString    = (fn_PrintString)   R(base + (OFF_PrintString    & ~1u));

    // 4. Hook CHud::Draw — ini yang bikin nametag muncul setiap frame
    //    Termasuk setelah relog, karena CHud::Draw terus berjalan
    void* hud_addr = T(base + (OFF_HudDraw & ~1u));
    int   rc       = DobbyHook(hud_addr, (void*)hooked_HudDraw,
                               (void**)&gOrigHudDraw);
    if (rc != 0) {
        LOGE("[nametag] DobbyHook CHud::Draw FAILED (rc=%d)", rc);
        return;
    }
    LOGI("[nametag] CHud::Draw hooked at 0x%08X", (unsigned)(base + (OFF_HudDraw & ~1u)));

    // 5. Mulai background watcher (hot-reload name.txt)
    pthread_t tid;
    pthread_create(&tid, nullptr, name_watcher, nullptr);
    pthread_detach(tid);

    g_ready = true;
    LOGI("[nametag] Ready! Name = '%s'", g_name_utf8);
}
