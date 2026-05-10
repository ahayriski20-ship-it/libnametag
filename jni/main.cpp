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

struct CRGBA { unsigned char r, g, b, a; };
typedef unsigned short gw;

typedef void (*fn_PS)(float,float,const gw*);
typedef void (*fn_SC)(CRGBA*); 
typedef void (*fn_SS)(float);
typedef void (*fn_SO)(unsigned char);
typedef void (*fn_SD)(signed char);
typedef void (*fn_SF)(unsigned char);
typedef void (*fn_SE)(signed char);
typedef void (*fn_HD)();

#define OFF_PS  0x5AA191u
#define OFF_SC  0x5AAFC9u
#define OFF_SS  0x5AB109u
#define OFF_SO  0x5AB305u
#define OFF_SD  0x5A8A6Du
#define OFF_SF  0x5AB14Du
#define OFF_SE  0x5AB27Du
#define OFF_HD  0x43A659u 

static fn_PS gPS; static fn_SC gSC; static fn_SS gSS;
static fn_SO gSO; static fn_SD gSD; static fn_SF gSF;
static fn_SE gSE; static fn_HD gOHD;

static gw g_wide[256]={};
static bool g_ready = false;

// --- CONFIG DEFAULT SESUAI VIDEO ---
// Posisi X=180 dan Y=410 untuk posisi bawah tengah layar
static char g_text[256] = "Khong tim thay mashiroNeiko.asi";
static float g_posX = 180.0f;
static float g_posY = 410.0f; 
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

static void PrintText(float x, float y, CRGBA color) {
    gSC(&color);
    gPS(x, y, g_wide);
}

static void draw_watermark(){
    if(!gPS || !gSC || !gSS) return;
    
    if(gSF) gSF(1); 
    if(gSD) gSD(0); 
    if(gSE) gSE(0); 
    if(gSO) gSO(1); 
    
    gSS(g_scale);
    
    CRGBA shadow = {0, 0, 0, 255}; 
    CRGBA text   = {255, 255, 255, 255}; 
    
    // Offset shadow 1 lapis saja untuk FIX CRASH sprite limit
    float offset = 1.0f * g_scale;
    
    PrintText(g_posX + offset, g_posY + offset, shadow);
    PrintText(g_posX, g_posY, text);
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
        static const char* info = "riski|1.1|Fixed Watermark Khong asi|ahayriski";
        return (void*)info;
    }

    EXPORT void OnModLoad() {
        LOGI("[MOD] OnModLoad mulai...");
        
        dl_iterate_phdr(find_lib_base, nullptr);
        if (g_libGTASA == 0) {
            LOGI("[MOD] Gagal menemukan base address libGTASA.so!");
            return;
        }

        void* hDobby = dlopen("libdobby.so", RTLD_NOW | RTLD_GLOBAL);
        if (!hDobby) return;

        auto dobbyHook = (int(*)(void*,void*,void**)) dlsym(hDobby, "DobbyHook");
        if (!dobbyHook) return;

        gSC = (fn_SC)T_PTR(g_libGTASA + OFF_SC);
        gSS = (fn_SS)T_PTR(g_libGTASA + OFF_SS);
        gSO = (fn_SO)T_PTR(g_libGTASA + OFF_SO);
        gSD = (fn_SD)T_PTR(g_libGTASA + OFF_SD);
        gSF = (fn_SF)T_PTR(g_libGTASA + OFF_SF);
        gSE = (fn_SE)T_PTR(g_libGTASA + OFF_SE);
        gPS = (fn_PS)T_PTR(g_libGTASA + OFF_PS);

        load_config();

        void* target = (void*)T_PTR(g_libGTASA + OFF_HD);
        if (dobbyHook(target, (void*)hook_DrawAfterFade, (void**)&gOHD) == 0) {
            g_ready = true; 
        }
    }
}
