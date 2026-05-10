#include <jni.h>
#include <android/log.h>
#include <dlfcn.h>
#include <cstring>
#include <cstdint>

#define TAG "libriski"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, TAG, __VA_ARGS__)
#define EXPORT __attribute__((visibility("default")))

typedef unsigned short gw;
typedef void (*fn_PS)(float,float,const gw*);
typedef void (*fn_SC)(unsigned char,unsigned char,unsigned char,unsigned char);
typedef void (*fn_SS)(float,float);
typedef void (*fn_SO)(int);
typedef void (*fn_SD)(int);
typedef void (*fn_SF)(int);
typedef void (*fn_SE)(int);
typedef void (*fn_HD)();

// OFFSET GTA SA v1.08
#define OFF_PS  0x583D70u
#define OFF_SC  0x582D00u
#define OFF_SS  0x582C9Cu
#define OFF_SO  0x582CE4u
#define OFF_SD  0x582D68u
#define OFF_SF  0x582C64u
#define OFF_SE  0x582D8Cu
#define OFF_HD  0x43A3FCu // CHud::Draw (v1.08)

static fn_PS gPS; static fn_SC gSC; static fn_SS gSS;
static fn_SO gSO; static fn_SD gSD; static fn_SF gSF;
static fn_SE gSE; static fn_HD gOHD;

static gw g_wide[256]={};

static void tw(const char*s, gw*d, int m){
    int i=0;
    while(*s && i<m-1) d[i++]=(gw)(unsigned char)*s++;
    d[i]=0;
}

static void draw_watermark(){
    if(!gPS || !gSC || !gSS) return;
    
    // Posisi Watermark
    const float X = 350.0f; 
    const float Y = 30.0f;
    
    if(gSF) gSF(1); 
    if(gSD) gSD(0);
    if(gSE) gSE(1); 
    
    // Bayangan Hitam
    gSC(0,0,0,200); gSS(0.5f,1.0f); if(gSO) gSO(0); gPS(X+1.5f, Y+1.5f, g_wide);
    
    // Teks Putih
    if(gSE) gSE(0); 
    gSC(255,255,255,255); gSS(0.5f,1.0f); if(gSO) gSO(0); gPS(X, Y, g_wide);
}

// Hook kita
static void hook_CHud_Draw(){
    if(gOHD) ((fn_HD)gOHD)(); // Eksekusi HUD game asli
    draw_watermark();         // Tambahkan teks "Riski Aja"
}

extern "C" {
    // Info Mod untuk AML (Wajib ada biar gak force close)
    EXPORT void* __GetModInfo() {
        static const char* info = "riski|1.0|Watermark Riski|ahayriski";
        return (void*)info;
    }

    EXPORT void OnModLoad() {
        LOGI("Memuat Watermark Riski Aja...");
        tw("Riski Aja", g_wide, 256); // Hardcode teks
        
        void* hDobby = dlopen("libdobby.so", RTLD_NOW | RTLD_GLOBAL);
        if (!hDobby) { LOGI("ERROR: libdobby.so tidak ditemukan!"); return; }
        
        auto dobbyHook = (int(*)(void*,void*,void**)) dlsym(hDobby, "DobbyHook");
        if (!dobbyHook) { LOGI("ERROR: DobbyHook tidak ditemukan!"); return; }

        void* hGTASA = dlopen("libGTASA.so", RTLD_NOW | RTLD_NOLOAD);
        if (!hGTASA) { LOGI("ERROR: libGTASA.so belum di-load!"); return; }

        uintptr_t b = (uintptr_t)hGTASA;
        LOGI("libGTASA base = 0x%08X", (unsigned)b);

        #define T_PTR(a) ((a) | 1u)
        gSC = (fn_SC)T_PTR(b + OFF_SC);
        gSS = (fn_SS)T_PTR(b + OFF_SS);
        gSO = (fn_SO)T_PTR(b + OFF_SO);
        gSD = (fn_SD)T_PTR(b + OFF_SD);
        gSF = (fn_SF)T_PTR(b + OFF_SF);
        gSE = (fn_SE)T_PTR(b + OFF_SE);
        gPS = (fn_PS)T_PTR(b + OFF_PS);

        // Pasang DobbyHook dengan instruksi Thumb (T_PTR)
        void* target = (void*)T_PTR(b + OFF_HD);
        if (dobbyHook(target, (void*)hook_CHud_Draw, (void**)&gOHD) == 0) {
            LOGI("HOOK CHud::Draw BERHASIL BOSKU!");
        } else {
            LOGI("ERROR: DobbyHook gagal menginjeksi");
        }
    }
}
