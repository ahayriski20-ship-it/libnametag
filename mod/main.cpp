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

static void logff(const char* fmt, ...) {
    char buf[512]; va_list ap;
    va_start(ap, fmt); vsnprintf(buf, sizeof(buf), fmt, ap); va_end(ap);
    __android_log_print(ANDROID_LOG_INFO, LOG_TAG, "%s", buf);
    FILE* f = fopen(LOGFILE, "a");
    if (f) { fprintf(f, "%s\n", buf); fclose(f); }
}

typedef unsigned short gw;
typedef void (*fn_HD)();
typedef void (*fn_PS)(float, float, const gw*);
typedef void (*fn_SC)(unsigned char,unsigned char,unsigned char,unsigned char);
typedef void (*fn_SS)(float, float);
typedef void (*fn_SO)(int);
typedef void (*fn_SD)(int);
typedef void (*fn_SF)(int);
typedef void (*fn_SE)(int);

// ── Offset libGTASA.so (Thumb2, ARM32) ───────────────────────
// Base dari crash log kamu: 0xC089D000
// Verifikasi: offset ini untuk ro.alyn_sampmobile.game
#define OFF_HD  0x58E918u   // CHud::Draw
#define OFF_PS  0x5AA190u   // CFont::PrintString
#define OFF_SC  0x5A8C10u   // CFont::SetColor
#define OFF_SS  0x5A8B90u   // CFont::SetScale
#define OFF_SO  0x5A8BD8u   // CFont::SetOrientation
#define OFF_SD  0x5A8C50u   // CFont::SetDropShadow
#define OFF_SF  0x5A8B50u   // CFont::SetFontStyle
#define OFF_SE  0x5A8C70u   // CFont::SetEdge

static fn_HD gOHD = nullptr;
static fn_PS gPS  = nullptr;
static fn_SC gSC  = nullptr;
static fn_SS gSS  = nullptr;
static fn_SO gSO  = nullptr;
static fn_SD gSD  = nullptr;
static fn_SF gSF  = nullptr;
static fn_SE gSE  = nullptr;

static char g_utf8[256] = "PlayerName";
static gw   g_wide[256] = {};
static bool g_ready     = false;
static pthread_mutex_t g_mtx = PTHREAD_MUTEX_INITIALIZER;

static const char* kPaths[] = {
    "/storage/emulated/0/name.txt",
    "/sdcard/name.txt",
    nullptr
};

static void to_wide(const char* s, gw* d, int m) {
    int i = 0;
    while (*s && i < m-1) d[i++] = (gw)(unsigned char)*s++;
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
        int n = (int)strlen(b);
        while (n > 0 && (b[n-1]=='\n'||b[n-1]=='\r')) b[--n] = 0;
        if (!n) continue;
        pthread_mutex_lock(&g_mtx);
        strncpy(g_utf8, b, 255); g_utf8[255]=0;
        to_wide(g_utf8, g_wide, 256);
        pthread_mutex_unlock(&g_mtx);
        logff("[nametag] nama=%s", g_utf8);
        return;
    }
    pthread_mutex_lock(&g_mtx);
    to_wide(g_utf8, g_wide, 256);
    pthread_mutex_unlock(&g_mtx);
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
    // Tengah agak kanan (320=tengah, 370=kanan sedikit), Y=30 atas
    const float X = 370.0f, Y = 30.0f;
    if (gSF) gSF(1);
    if (gSD) gSD(0);
    // Shadow
    if (gSE) gSE(1);
    gSC(0,0,0,220); gSS(0.6f,1.2f);
    if (gSO) gSO(0);
    gPS(X+1.5f, Y+1.5f, buf);
    // Teks putih
    if (gSE) gSE(0);
    gSC(255,255,255,255); gSS(0.6f,1.2f);
    if (gSO) gSO(0);
    gPS(X, Y, buf);
}

static void hooked_HudDraw() {
    gOHD();
    if (g_ready) render_nametag();
}

// ── BENAR: baca base dari /proc/self/maps ────────────────────
static uintptr_t get_lib_base(const char* libname) {
    char maps[64];
    snprintf(maps, sizeof(maps), "/proc/%d/maps", getpid());
    FILE* f = fopen(maps, "r");
    if (!f) return 0;
    char line[512];
    uintptr_t base = 0;
    while (fgets(line, sizeof(line), f)) {
        if (strstr(line, libname) && strstr(line, "r-xp")) {
            base = (uintptr_t)strtoul(line, nullptr, 16);
            break;
        }
    }
    fclose(f);
    return base;
}

extern "C" {

EXPORT void* __GetModInfo() {
    static const char* info = "nametag|1.0|Nametag dari name.txt|ahayriski";
    return (void*)info;
}

EXPORT void OnModPreLoad() {
    remove(LOGFILE);
    logff("[nametag] OnModPreLoad");
}

EXPORT void OnModLoad() {
    logff("[nametag] OnModLoad");
    load_name();

    // 1. Cari base libGTASA.so via /proc/self/maps (BUKAN dlopen!)
    uintptr_t base = get_lib_base("libGTASA.so");
    if (!base) {
        logff("[nametag] ERROR: libGTASA.so base tidak ditemukan");
        return;
    }
    logff("[nametag] base=0x%08X", (unsigned)base);

    // 2. Resolve CFont — alamat absolut = base + offset
    //    Thumb: simpan sebagai raw (tanpa |1), panggil via pointer akan otomatis
    gSC = (fn_SC)(base + OFF_SC);
    gSS = (fn_SS)(base + OFF_SS);
    gSO = (fn_SO)(base + OFF_SO);
    gSD = (fn_SD)(base + OFF_SD);
    gSF = (fn_SF)(base + OFF_SF);
    gSE = (fn_SE)(base + OFF_SE);
    gPS = (fn_PS)(base + OFF_PS);

    logff("[nametag] CFont resolved: PS=0x%08X SC=0x%08X",
          (unsigned)(base+OFF_PS), (unsigned)(base+OFF_SC));

    // 3. Dobby via dlopen — sudah ada di game
    void* hDobby = dlopen("libdobby.so", RTLD_NOW | RTLD_GLOBAL);
    if (!hDobby) {
        logff("[nametag] ERROR: libdobby.so tidak ada: %s", dlerror());
        return;
    }

    auto dobbyHook = (int(*)(void*,void*,void**))dlsym(hDobby, "DobbyHook");
    if (!dobbyHook) {
        logff("[nametag] ERROR: DobbyHook tidak ditemukan");
        return;
    }
    logff("[nametag] DobbyHook OK");

    // 4. Hook CHud::Draw
    // Thumb2: alamat harus di-OR 1 supaya Dobby tahu ini Thumb
    uintptr_t hud_raw  = base + OFF_HD;
    void*     hud_addr = (void*)(hud_raw | 1u);
    logff("[nametag] hooking CHud::Draw di 0x%08X", (unsigned)hud_raw);

    int rc = dobbyHook(hud_addr, (void*)hooked_HudDraw, (void**)&gOHD);
    if (rc != 0) {
        logff("[nametag] ERROR: DobbyHook gagal rc=%d", rc);
        return;
    }
    if (!gOHD) {
        logff("[nametag] ERROR: trampoline null setelah hook");
        return;
    }
    logff("[nametag] CHud::Draw hooked! trampoline=0x%08X", (unsigned)(uintptr_t)gOHD);

    // 5. Watcher thread
    pthread_t tid;
    pthread_create(&tid, nullptr, watcher_thread, nullptr);
    pthread_detach(tid);

    g_ready = true;
    logff("[nametag] SIAP! nama=%s", g_utf8);
}

} // extern "C"
