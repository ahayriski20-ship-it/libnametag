#include <jni.h>
#include <android/log.h>
#include <dlfcn.h>
#include <link.h>
#include <cstring>
#include <cstdint>
#include <pthread.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>

#define TAG "libriski"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, TAG, __VA_ARGS__)
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
typedef void (*fn_SP)(unsigned char); // SetProportional
typedef void (*fn_TU)(); // CTimer::Update

#define OFF_PS  0x5AA191u
#define OFF_SC  0x5AAFC9u
#define OFF_SS  0x5AB109u
#define OFF_SO  0x5AB305u
#define OFF_SD  0x5A8A6Du
#define OFF_SF  0x5AB14Du
#define OFF_SE  0x5AB27Du
#define OFF_SP  0x005ab2b1u
#define OFF_TU  0x00420be5u

static fn_PS gPS; static fn_SC gSC; static fn_SS gSS;
static fn_SO gSO; static fn_SD gSD; static fn_SF gSF;
static fn_SE gSE; static fn_SP gSP_func; static fn_TU gOTU;

static gw g_wide[256]={};
static bool g_ready=false;

static char g_text[256] = "Riski Boren";
static float g_posX = 320.0f;
static float g_posY = 390.0f;
static float g_scale = 0.8f; // Default baru agar padat

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
        LOGI("Config dimuat: '%s', X=%.1f, Y=%.1f, S=%.1f", g_text, g_posX, g_posY, g_scale);
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
    if(!gPS || !gSC || !gSS) return;
    
    if(gSF) gSF(2); // FONT STYLE 2 (Lebih padat)
    if(gSD) gSD(0); // No drop shadow
    if(gSP_func) gSP_func(1); // SetProportional(TRUE) = Font tidak patah-patah!
    if(gSE) gSE(2); // Outline ketebalan 2
    if(gSO) gSO(1); // Alignment Center
    
    CRGBA text = {255, 255, 255, 255};
    gSC(&text); 
    gSS(g_scale); 
    
    gPS(g_posX, g_posY, g_wide);
}

// Hook dipindah ke CTimer::Update (Jauh dari RenderWare dan LuaJIT)
static void hook_CTimer_Update(){
    if(gOTU) ((fn_TU)gOTU)(); // Jalankan timer asli
    if(g_ready) draw_watermark(); // Sisipkan render kita
}

static int find_lib_base(struct dl_phdr_info *info, size_t size, void *data) {
    if (strstr(info->dlpi_name, "libGTASA.so")) {
        uintptr_t *base = (uintptr_t *)data;
        *base = info->dlpi_addr;
        return 1; 
    }
    return 0;
}

#define T_PTR(a) ((a) | 1u)

static void* init_thread(void*) {
    uintptr_t b = 0;
    while (b == 0) {
        dl_iterate_phdr(find_lib_base, &b);
        sleep(1);
    }
    
    sleep(5); 
    load_config();

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
    gSP_func = (fn_SP)T_PTR(b + OFF_SP);
    gPS = (fn_PS)T_PTR(b + OFF_PS);

    void* target = (void*)T_PTR(b + OFF_TU);
    if (dobbyHook(target, (void*)hook_CTimer_Update, (void**)&gOTU) == 0) {
        g_ready = true; 
        LOGI("HOOK CTimer::Update BERHASIL! Anti LuaJIT Crash!");
    }
    return nullptr;
}

extern "C" {
    EXPORT void* __GetModInfo() {
        static const char* info = "riski|1.0|Dynamic Config Watermark|ahayriski";
        return (void*)info;
    }

    EXPORT void OnModLoad() {
        tw("Loading...", g_wide, 256); 
        pthread_t t;
        pthread_create(&t, nullptr, init_thread, nullptr);
        pthread_detach(t);
    }
}
