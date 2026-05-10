#include <jni.h>
#include <android/log.h>
#include <dlfcn.h>
#include <link.h>
#include <cstring>
#include <cstdint>
#include <pthread.h>
#include <unistd.h>

#define TAG "libriski"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, TAG, __VA_ARGS__)
#define EXPORT __attribute__((visibility("default")))

// FIX: Struct CRGBA wajib agar sesuai dengan ARM32 Calling Convention
struct CRGBA { unsigned char r, g, b, a; };
typedef unsigned short gw;

// Signature sesuai hasil ReadELF!
typedef void (*fn_PS)(float,float,const gw*);
typedef void (*fn_SC)(CRGBA);
typedef void (*fn_SS)(float,float);
typedef void (*fn_SO)(unsigned char);
typedef void (*fn_SD)(signed char);
typedef void (*fn_SF)(unsigned char);
typedef void (*fn_SE)(signed char);
typedef void (*fn_HD)();

// OFFSET CUSTOM DARI HASIL READELF KAMU
#define OFF_PS  0x5AA191u
#define OFF_SC  0x5AAFC9u
#define OFF_SS  0x5AB109u
#define OFF_SO  0x5AB305u
#define OFF_SD  0x5A8A6Du
#define OFF_SF  0x5AB14Du
#define OFF_SE  0x5AB27Du
#define OFF_HD  0x43A659u // DrawAfterFade

static fn_PS gPS; static fn_SC gSC; static fn_SS gSS;
static fn_SO gSO; static fn_SD gSD; static fn_SF gSF;
static fn_SE gSE; static fn_HD gOHD;

static gw g_wide[256]={};
static bool g_ready=false;

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
    
    // Terapkan Shadow
    CRGBA shadow = {0, 0, 0, 200};
    gSC(shadow); gSS(0.5f, 1.0f); if(gSO) gSO(0); gPS(X+1.5f, Y+1.5f, g_wide);
    
    // Terapkan Teks Utama
    if(gSE) gSE(0); 
    CRGBA text = {255, 255, 255, 255};
    gSC(text); gSS(0.5f, 1.0f); if(gSO) gSO(0); gPS(X, Y, g_wide);
}

// Hook dipasang di DrawAfterFade, dijamin tidak tabrakan dengan UI SAMP
static void hook_DrawAfterFade(){
    if(gOHD) ((fn_HD)gOHD)();
    if(g_ready) draw_watermark();
}

// Fungsi Android API paling aman untuk mencari Base Address (Bebas file I/O crash)
static int find_lib_base(struct dl_phdr_info *info, size_t size, void *data) {
    if (strstr(info->dlpi_name, "libGTASA.so")) {
        uintptr_t *base = (uintptr_t *)data;
        *base = info->dlpi_addr;
        return 1; // Ketemu!
    }
    return 0;
}

#define T_PTR(a) ((a) | 1u)

static void* init_thread(void*) {
    uintptr_t b = 0;
    // Looping aman sampai libGTASA termuat di memori
    while (b == 0) {
        dl_iterate_phdr(find_lib_base, &b);
        sleep(1);
    }
    
    LOGI("libGTASA Ditemukan di: 0x%08X", (unsigned)b);

    void* hDobby = dlopen("libdobby.so", RTLD_NOW | RTLD_GLOBAL);
    if (!hDobby) return nullptr;
    auto dobbyHook = (int(*)(void*,void*,void**)) dlsym(hDobby, "DobbyHook");
    if (!dobbyHook) return nullptr;

    gSC = (fn_SC)T_PTR(b + OFF_SC);
    gSS = (fn_SS)T_PTR(b + OFF_SS);
    gSO = (fn_SO)T_PTR(b + OFF_SO);
    gSD = (fn_SD)T_PTR(b + OFF_SD);
    gSF = (fn_SF)T_PTR(b + OFF_SF);
    gSE = (fn_SE)T_PTR(b + OFF_SE);
    gPS = (fn_PS)T_PTR(b + OFF_PS);

    void* target = (void*)T_PTR(b + OFF_HD);
    if (dobbyHook(target, (void*)hook_DrawAfterFade, (void**)&gOHD) == 0) {
        g_ready = true; 
        LOGI("HOOK DrawAfterFade BERHASIL!");
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
