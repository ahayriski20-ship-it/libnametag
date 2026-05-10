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
static bool g_ready=false;

// Variabel Global untuk Konfigurasi
static char g_text[256] = "Riski Boren";
static float g_posX = 320.0f;
static float g_posY = 390.0f;
static float g_scale = 1.2f;

static void tw(const char*s, gw*d, int m){
    int i=0;
    while(*s && i<m-1) d[i++]=(gw)(unsigned char)*s++;
    d[i]=0;
}

// MESIN PEMBACA CONFIG
static void load_config() {
    const char* path = "/storage/emulated/0/riski_config.txt";
    FILE* f = fopen(path, "r");
    
    if (f) {
        char line[256];
        // Baris 1: Teks
        if (fgets(line, sizeof(line), f)) {
            line[strcspn(line, "\r\n")] = 0;
            if(strlen(line) > 0) strcpy(g_text, line);
        }
        // Baris 2: Posisi X
        if (fgets(line, sizeof(line), f)) sscanf(line, "%f", &g_posX);
        // Baris 3: Posisi Y
        if (fgets(line, sizeof(line), f)) sscanf(line, "%f", &g_posY);
        // Baris 4: Ukuran/Skala
        if (fgets(line, sizeof(line), f)) sscanf(line, "%f", &g_scale);
        
        fclose(f);
        LOGI("Config berhasil dimuat: Teks='%s', X=%.1f, Y=%.1f, Scale=%.1f", g_text, g_posX, g_posY, g_scale);
    } else {
        // Kalau file belum ada, otomatis bikin file default!
        f = fopen(path, "w");
        if (f) {
            fprintf(f, "%s\n%.1f\n%.1f\n%.1f\n", g_text, g_posX, g_posY, g_scale);
            fclose(f);
            LOGI("File config default berhasil dibuat di %s", path);
        }
    }
    
    // Terapkan teks
    tw(g_text, g_wide, 256);
}

static void draw_watermark(){
    if(!gPS || !gSC || !gSS) return;
    
    if(gSF) gSF(2); // Font Tebal
    if(gSD) gSD(0); // Matikan DropShadow
    if(gSE) gSE(2); // Nyalakan Outline Tebal
    if(gSO) gSO(1); // Set Alignment ke CENTER (Tengah)
    
    // Terapkan Warna Teks & Skala dari Config
    CRGBA text = {255, 255, 255, 255};
    gSC(&text); 
    gSS(g_scale); 
    
    // Gambar teks di posisi X,Y dari Config
    gPS(g_posX, g_posY, g_wide);
}

static void hook_DrawAfterFade(){
    if(gOHD) ((fn_HD)gOHD)();
    if(g_ready) draw_watermark();
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

    // Muat konfigurasi sesaat sebelum hook aktif!
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
    gPS = (fn_PS)T_PTR(b + OFF_PS);

    void* target = (void*)T_PTR(b + OFF_HD);
    if (dobbyHook(target, (void*)hook_DrawAfterFade, (void**)&gOHD) == 0) {
        g_ready = true; 
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
