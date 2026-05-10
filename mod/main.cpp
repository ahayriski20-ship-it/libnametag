#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <dlfcn.h>
#include <unistd.h>
#include <pthread.h>
#include <android/log.h>

#define LOG_TAG "libnametag"
#define LOGFILE "/storage/emulated/0/nametag_log.txt"
#define EXPORT  __attribute__((visibility("default")))

static void logf_(const char* msg) {
    __android_log_print(ANDROID_LOG_INFO, LOG_TAG, "%s", msg);
    FILE* f = fopen(LOGFILE, "a");
    if (f) { fprintf(f, "%s\n", msg); fclose(f); }
}
static void logff(const char* fmt, ...) {
    char buf[256]; va_list ap;
    va_start(ap, fmt); vsnprintf(buf, sizeof(buf), fmt, ap); va_end(ap);
    logf_(buf);
}

// ── Tipe GTA wide char ────────────────────────────────────────
typedef unsigned short gw;

// ── Offset CHud::Draw & CFont (libGTASA.so, ARM Thumb2) ──────
#define OFF_HD  0x58E919u
#define OFF_PS  0x5AA191u
#define OFF_SC  0x5A8C11u
#define OFF_SS  0x5A8B91u
#define OFF_SO  0x5A8BD9u
#define OFF_SD  0x5A8C51u
#define OFF_SF  0x5A8B51u
#define OFF_SE  0x5A8C71u

typedef void (*fn_HD)();
typedef void (*fn_PS)(float, float, const gw*);
typedef void (*fn_SC)(unsigned char, unsigned char, unsigned char, unsigned char);
typedef void (*fn_SS)(float, float);
typedef void (*fn_SO)(int);
typedef void (*fn_SD)(int);
typedef void (*fn_SF)(int);
typedef void (*fn_SE)(int);

static fn_HD gOHD;
static fn_PS gPS; static fn_SC gSC; static fn_SS gSS;
static fn_SO gSO; static fn_SD gSD; static fn_SF gSF; static fn_SE gSE;

// ── State nama ────────────────────────────────────────────────
static char g_utf8[256] = "PlayerName";
static gw   g_wide[256] = {};
static bool g_ready     = false;
static pthread_mutex_t g_mtx = PTHREAD_MUTEX_INITIALIZER;

static const char* kPaths[] = {
    "/storage/emulated/0/name.txt",
    "/sdcard/name.txt",
    "/storage/emulated/0/SAMP/name.txt",
    "/storage/emulated/0/Android/data/com.sampmobilerp.game/mods/name.txt",
    nullptr
};

static void to_wide(const char* s, gw* d, int m) {
    int i = 0;
    while (*s && i < m - 1) d[i++] = (gw)(unsigned char)*s++;
    d[i] = 0;
}

static void load_name() {
    for (int i = 0; kPaths[i]; i++) {
        FILE* f = fopen(kPaths[i], "r");
        if (!f) continue;
        char b[256] = {};
        bool ok = fgets(b, 256, f) != nullptr;
        fclose(f);
        if (!ok) continue;
        int n = strlen(b);
        while (n > 0 && (b[n-1] == '\n' || b[n-1] == '\r')) b[--n] = 0;
        if (!n) continue;
        pthread_mutex_lock(&g_mtx);
        strncpy(g_utf8, b, 255);
        to_wide(g_utf8, g_wide, 256);
        pthread_mutex_unlock(&g_mtx);
        logff("[nametag] name=%s dari %s", g_utf8, kPaths[i]);
        return;
    }
    pthread_mutex_lock(&g_mtx);
    to_wide(g_utf8, g_wide, 256);
    pthread_mutex_unlock(&g_mtx);
    logff("[nametag] name.txt tidak ditemukan, pakai default: %s", g_utf8);
}

static void* watcher_thread(void*) {
    while (true) { sleep(3); load_name(); }
    return nullptr;
}

static void render_nametag() {
    if (!gPS || !gSC || !gSS) return;
    gw buf[256];
    pthread_mutex_lock(&g_mtx);
    memcpy(buf, g_wide, sizeof(g_wide));
    pthread_mutex_unlock(&g_mtx);

    // Posisi: tengah agak kanan (x=360), atas (y=45)
    const float X = 360.0f, Y = 45.0f;

    if (gSF) gSF(1);
    if (gSD) gSD(0);
    // Shadow hitam
    if (gSE) gSE(1);
    gSC(0, 0, 0, 200);
    gSS(0.50f, 1.0f);
    if (gSO) gSO(0);
    gPS(X + 1.5f, Y + 1.5f, buf);
    // Teks putih
    if (gSE) gSE(0);
    gSC(255, 255, 255, 255);
    gSS(0.50f, 1.0f);
    if (gSO) gSO(0);
    gPS(X, Y, buf);
}

static void hooked_HudDraw() {
    gOHD();
    if (g_ready) render_nametag();
}

extern "C" {

EXPORT void* __GetModInfo() {
    static const char* info = "nametag|1.0|AML Nametag dari name.txt|ahayriski";
    return (void*)info;
}

EXPORT void OnModPreLoad() {
    remove(LOGFILE);
    logf_("[nametag] OnModPreLoad");
}

EXPORT void OnModLoad() {
    logf_("[nametag] OnModLoad mulai");

    // 1. Load nama dari name.txt
    load_name();

    // 2. Ambil base libGTASA.so via dlopen NOLOAD
    void* hGTASA = dlopen("libGTASA.so", RTLD_NOW | RTLD_NOLOAD);
    if (!hGTASA) { logf_("[nametag] ERROR: libGTASA.so tidak ditemukan"); return; }
    uintptr_t base = (uintptr_t)hGTASA;
    logff("[nametag] base=0x%08X", (unsigned)base);

    // 3. Resolve CFont functions (tidak di-hook, langsung call)
    gSC = (fn_SC)((base + (OFF_SC & ~1u)));
    gSS = (fn_SS)((base + (OFF_SS & ~1u)));
    gSO = (fn_SO)((base + (OFF_SO & ~1u)));
    gSD = (fn_SD)((base + (OFF_SD & ~1u)));
    gSF = (fn_SF)((base + (OFF_SF & ~1u)));
    gSE = (fn_SE)((base + (OFF_SE & ~1u)));
    gPS = (fn_PS)((base + (OFF_PS & ~1u)));

    // 4. Dobby via dlopen — sudah ada di game, tidak perlu link statik
    void* hDobby = dlopen("libdobby.so", RTLD_NOW | RTLD_GLOBAL);
    if (!hDobby) { logf_("[nametag] ERROR: libdobby.so tidak ada"); return; }

    auto dobbyHook = (int(*)(void*, void*, void**))dlsym(hDobby, "DobbyHook");
    if (!dobbyHook) { logf_("[nametag] ERROR: DobbyHook sym tidak ditemukan"); return; }

    // 5. Hook CHud::Draw (Thumb: address | 1)
    void* hud_addr = (void*)((base + (OFF_HD & ~1u)) | 1u);
    logff("[nametag] hook CHud::Draw di 0x%08X", (unsigned)(uintptr_t)hud_addr);

    int rc = dobbyHook(hud_addr, (void*)hooked_HudDraw, (void**)&gOHD);
    if (rc != 0) {
        logff("[nametag] ERROR: DobbyHook gagal rc=%d", rc);
        return;
    }
    logf_("[nametag] CHud::Draw hooked OK");

    // 6. Mulai watcher thread (hot-reload name.txt tiap 3 detik)
    pthread_t tid;
    pthread_create(&tid, nullptr, watcher_thread, nullptr);
    pthread_detach(tid);

    g_ready = true;
    logff("[nametag] SIAP! nama=%s", g_utf8);
}

} // extern "C"
