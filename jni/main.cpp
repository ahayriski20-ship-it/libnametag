#include <jni.h>
#include <android/log.h>
#include <dlfcn.h>
#include <cstring>
#include <cstdint>
#include <pthread.h>
#include <unistd.h>
#include <fcntl.h>
#include <cstdlib>

#define TAG "libriski"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, TAG, __VA_ARGS__)
#define EXPORT __attribute__((visibility("default")))

// Struktur CRGBA 100% aman untuk ARM32 ABI Memory
struct CRGBA { unsigned char r, g, b, a; };
typedef unsigned short gw;
typedef void (*fn_PS)(float,float,const gw*);
typedef void (*fn_SC)(CRGBA);
typedef void (*fn_SS)(float,float);
typedef void (*fn_SO)(unsigned char);
typedef void (*fn_SD)(short);
typedef void (*fn_SF)(unsigned char);
typedef void (*fn_SE)(short);
typedef void (*fn_HD)();

#define OFF_PS  0x005aa191u
#define OFF_SC  0x005aafc9u
#define OFF_SS  0x005ab109u
#define OFF_SO  0x005ab305u
#define OFF_SD  0x005a8a6du
#define OFF_SF  0x005ab14du
#define OFF_SE  0x005ab27du
#define OFF_HD  0x0043a659u

static fn_PS gPS; static fn_SC gSC; static fn_SS gSS;
static fn_SO gSO; static fn_SD gSD; static fn_SF gSF;
static fn_SE gSE; static fn_HD gOHD;

static gw g_wide[256]={};
static bool g_ready=false;

static uint8_t s_tramp[16] __attribute__((aligned(4)));

static bool thumb_hook(uintptr_t target_addr, void* replace, void** orig) {
    uintptr_t addr = target_addr & ~1u; 
    int ps = getpagesize();
    void* page = (void*)(addr & ~(uintptr_t)(ps-1));
    if (mprotect(page, ps*2, PROT_READ|PROT_WRITE|PROT_EXEC) != 0) {
        LOGE("mprotect target failed"); return false; 
    }
    
    memcpy(s_tramp, (void*)addr, 8);
    uintptr_t ret = addr + 8;
    s_tramp[8]=0xDF; s_tramp[9]=0xF8; s_tramp[10]=0x00; s_tramp[11]=0xF0;
    memcpy(s_tramp+12, &ret, 4);
    
    void* tp = (void*)((uintptr_t)s_tramp & ~(uintptr_t)(ps-1));
    mprotect(tp, ps, PROT_READ|PROT_WRITE|PROT_EXEC);
    __builtin___clear_cache((char*)s_tramp, (char*)s_tramp+16);
    
    uintptr_t rep = (uintptr_t)replace;
    uint8_t patch[8]={0xDF,0xF8,0x00,0xF0};
    memcpy(patch+4, &rep, 4);
    memcpy((void*)addr, patch, 8);
    __builtin___clear_cache((char*)addr, (char*)addr+8);
    
    *orig = (void*)((uintptr_t)s_tramp | 1);
    return true;
}

static void tw(const char*s, gw*d, int m){
    int i=0;
    while(*s && i<m-1) d[i++]=(gw)(unsigned char)*s++;
    d[i]=0;
}

static void draw_watermark(){
    if(!gPS || !gSC || !gSS) return;
    
    const float X = 350.0f; 
    const float Y = 30.0f;
    
    if(gSF) gSF(1); 
    if(gSD) gSD(0);
    if(gSE) gSE(1); 
    
    CRGBA shadow = {0, 0, 0, 200};
    CRGBA text_color = {255, 255, 255, 255};

    gSC(shadow); gSS(0.5f, 1.0f); if(gSO) gSO(0); gPS(X+1.5f, Y+1.5f, g_wide);
    if(gSE) gSE(0); 
    gSC(text_color); gSS(0.5f, 1.0f); if(gSO) gSO(0); gPS(X, Y, g_wide);
}

static void hook_HD(){
    if(gOHD) ((fn_HD)gOHD)();
    if(g_ready) draw_watermark();
}

static uintptr_t get_base_posix(const char* lib_name) {
    int fd = open("/proc/self/maps", O_RDONLY);
    if (fd < 0) return 0;
    char buf[512]; uintptr_t base_addr = 0; ssize_t bytes_read;
    char line[512]; int line_pos = 0;
    while ((bytes_read = read(fd, buf, sizeof(buf))) > 0) {
        for (ssize_t i = 0; i < bytes_read; i++) {
            if (buf[i] == '\n' || i == bytes_read - 1) {
                line[line_pos] = '\0';
                if (strstr(line, lib_name) && strstr(line, "r-xp")) {
                    base_addr = (uintptr_t)strtoul(line, nullptr, 16);
                    close(fd); return base_addr;
                }
                line_pos = 0;
            } else { if (line_pos < 511) line[line_pos++] = buf[i]; }
        }
    }
    close(fd); return base_addr;
}

#define T_PTR(a) ((a) | 1u)

static void* init_thread(void*) {
    uintptr_t b = 0;
    while (!(b = get_base_posix("libGTASA.so"))) { sleep(1); }
    
    sleep(5); // Wajib tunggu 5 detik biarkan SAMP selesaikan loading!

    LOGI("libGTASA REAL BASE = 0x%08X", (unsigned)b);

    void* hDobby = dlopen("libdobby.so", RTLD_NOW | RTLD_GLOBAL);
    if (!hDobby) { LOGE("libdobby.so tidak ditemukan!"); return nullptr; }
    
    auto dobbyHook = (int(*)(void*,void*,void**)) dlsym(hDobby, "DobbyHook");
    if (!dobbyHook) { LOGE("DobbyHook tidak ditemukan!"); return nullptr; }

    gSC = (fn_SC)T_PTR(b + OFF_SC);
    gSS = (fn_SS)T_PTR(b + OFF_SS);
    gSO = (fn_SO)T_PTR(b + OFF_SO);
    gSD = (fn_SD)T_PTR(b + OFF_SD);
    gSF = (fn_SF)T_PTR(b + OFF_SF);
    gSE = (fn_SE)T_PTR(b + OFF_SE);
    gPS = (fn_PS)T_PTR(b + OFF_PS);

    void* target = (void*)T_PTR(b + OFF_HD);
    if (dobbyHook(target, (void*)hook_HD, (void**)&gOHD) == 0) {
        g_ready = true; 
        LOGI("HOOK CHud::DrawAfterFade BERHASIL!");
    } else {
        LOGE("DobbyHook gagal.");
    }
    return nullptr;
}

extern "C" {
    EXPORT void* __GetModInfo() {
        static const char* info = "riski|1.0|Watermark Riski|ahayriski";
        return (void*)info;
    }

    EXPORT void OnModLoad() {
        LOGI("Riski Aja Mod Loaded oleh AML!");
        tw("Riski Aja", g_wide, 256); 
        
        pthread_t t;
        pthread_create(&t, nullptr, init_thread, nullptr);
        pthread_detach(t);
    }
}
