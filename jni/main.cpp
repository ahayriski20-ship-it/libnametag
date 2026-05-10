#include <jni.h>
#include <android/log.h>
#include <dlfcn.h>
#include <link.h>
#include <cstring>
#include <cstdint>
#include <stdio.h>
#include <stdlib.h>

#define TAG "libriski"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, TAG, __VA_ARGS__)
#define EXPORT __attribute__((visibility("default")))

typedef unsigned short gw;

// ABI Fix: Menggunakan uint32_t untuk fungsi warna agar aman dari struct pointer corruption
typedef void (*fn_PS)(float,float,const gw*);
typedef void (*fn_SC)(uint32_t); 
typedef void (*fn_SS)(float);
typedef void (*fn_SO)(unsigned char);
typedef void (*fn_SF)(unsigned char);
typedef void (*fn_HD)();

#define OFF_PS  0x5AA191u
#define OFF_SC  0x5AAFC9u
#define OFF_SS  0x5AB109u
#define OFF_SO  0x5AB305u
#define OFF_SF  0x5AB14Du
#define OFF_HD  0x43A659u 

static fn_PS gPS; static fn_SC gSC; static fn_SS gSS;
static fn_SO gSO; static fn_SF gSF; static fn_HD gOHD;

// Alokasi ke heap (dinamis) agar aman dari mapping deletion
static gw* g_wide = nullptr;
static bool g_ready = false;

static char g_text[256] = "Khong tim thay mashiroNeiko.asi";
static float g_posX = 150.0f; // Digeser agar presisi di rasio 20:9
static float g_posY = 350.0f; // Dinaikkan sedikit ke atas agar PASTI MUNCUL di layar
static float g_scale = 0.8f;  

static void tw(const char*s, gw*d, int m){
    int i=0;
    while(*s && i<m-1) d[i++]=(gw)(unsigned char)*s++;
    d[i]=0;
}

static void load_config() {
    const char* path = "/storage/emulated/0/riski_config.txt";
    FILE* f = fopen(path, "r");
    
    if (f) {
        char line[256];
        if (fgets(line, sizeof(line), f)) {
            line[strcspn(line, "\r\n")] = 0;
            if(strlen(line) > 0) strcpy(g_text, line);
        }
        if (fgets(line, sizeof(line), f)) sscanf(line, "%f", &g_posX);
        if (fgets(line, sizeof(line), f)) sscanf(line, "%f", &g_posY);
        if (fgets(line, sizeof(line), f)) sscanf(line, "%f", &g_scale);
        fclose(f);
    } else {
        f = fopen(path, "w");
        if (f) {
            fprintf(f, "%s\n%.1f\n%.1f\n%.1f\n", g_text, g_posX, g_posY, g_scale);
            fclose(f);
        }
    }
    tw(g_text, g_wide, 256);
}

static void draw_watermark(){
    if(!gPS || !gSC || !gSS || !g_wide) return;
    
    if(gSF) gSF(1); 
    if(gSO) gSO(1); 
    gSS(g_scale);
    
    // Format warna hex standar ARGB/ABGR (Aman untuk memory softfp)
    uint32_t shadow = 0xFF000000; // Hitam
    uint32_t text   = 0xFFFFFFFF; // Putih
    
    float offset = 1.0f * g_scale;
    
    // 1. Render Bayangan Manual (Aman 100%)
    gSC(shadow);
    gPS(g_posX + offset, g_posY + offset, g_wide);
    
    // 2. Render Teks Utama
    gSC(text);
    gPS(g_posX, g_posY, g_wide);
}

static void hook_DrawAfterFade(){
    if(gOHD) ((fn_HD)gOHD)(); 
    if(g_ready) draw_watermark();
}

static uintptr_t g_libGTASA = 0;
static int find_lib_base(struct dl_phdr_info *info, size_t size, void *data) {
    if (strstr(info->dlpi_name, "libGTASA.so")) {
        g_libGTASA = info->dlpi_addr;
        return 1; 
    }
    return 0;
}

#define T_PTR(a) ((a) | 1u)

extern "C" {
    EXPORT void* __GetModInfo() {
        static const char* info = "riski|1.2|Ultimate Fix Watermark|ahayriski";
        return (void*)info;
    }

    EXPORT void OnModLoad() {
        LOGI("[MOD] OnModLoad mulai...");
        
        // Alokasi memori secara dinamis untuk buffer string
        g_wide = new gw[256];
        memset(g_wide, 0, sizeof(gw) * 256);
        
        dl_iterate_phdr(find_lib_base, nullptr);
        if (g_libGTASA == 0) return;

        void* hDobby = dlopen("libdobby.so", RTLD_NOW | RTLD_GLOBAL);
        if (!hDobby) return;

        auto dobbyHook = (int(*)(void*,void*,void**)) dlsym(hDobby, "DobbyHook");
        if (!dobbyHook) return;

        gSC = (fn_SC)T_PTR(g_libGTASA + OFF_SC);
        gSS = (fn_SS)T_PTR(g_libGTASA + OFF_SS);
        gSO = (fn_SO)T_PTR(g_libGTASA + OFF_SO);
        gSF = (fn_SF)T_PTR(g_libGTASA + OFF_SF);
        gPS = (fn_PS)T_PTR(g_libGTASA + OFF_PS);

        load_config();

        void* target = (void*)T_PTR(g_libGTASA + OFF_HD);
        if (dobbyHook(target, (void*)hook_DrawAfterFade, (void**)&gOHD) == 0) {
            g_ready = true; 
        }
    }
}
