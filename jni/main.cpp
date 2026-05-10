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

// --- UPDATE: TEKS HURUF KECIL & POSISI TENGAH BAWAH ---
static char g_text[256] = "khong tim thay mashironeiko.asi";
static float g_posX = 320.0f; // TENGAH LAYAR (Posisi aman untuk semua HP)
static float g_posY = 420.0f; // AREA BAWAH SEKALIAN (Benar-benar bawah kaki)
static float g_scale = 0.9f;  // Skala diperkecil agar teks super panjang muat di tengah layar tanpa terpotong kanan-kiri

static void tw(const char*s, gw*d, int m){
    int i=0;
    while(s[i] && i < m - 1) {
        d[i] = (gw)(unsigned char)s[i];
        i++;
    }
    d[i]=0;
}

static void load_config() {
    // PENTING: Nama file baru agar otomatis reset pengaturan posisi ter-set ke tengah
    const char* path = "/storage/emulated/0/mashiro_tengah_fixed.txt";
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
    if(!gPS || !gSC || !gSS || !gSF || !gSD || !gSO || !gSE) return;
    
    // --- FIX BUG KETIKA MATI (RESET STATE FONT TOTAL) ---
    // Game sering merusak state font saat mati (tombol wasted/fasted muncul).
    // Kita reset total settingannya sesaat sebelum kita gambar teks kita sendiri.
    gSF(2);        // Reset Font style/texture
    gSD(0);        // Reset Shadow/Edge mode
    gSO(1);        // Reset Outline/Proportional setting
    gSE(1);        // Nyalakan Native Outline agar teks terlihat jelas (Tetap Anti-Crash)
    gSS(0.1f);     // Reset skala dulu
    
    // Terapkan skala sebenarnya
    gSS(g_scale); 
    
    // Warna teks utama (Putih, Alpha 255)
    CRGBA text_color = {255, 255, 255, 255}; 
    
    // Gambar Teks (HANYA 1 KALI PANGGIL untuk performa & kestabilan)
    PrintText(g_posX, g_posY, text_color);
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
    
    sleep(6); 
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
        static const char* info = "mashiro|1.5|Fixed Position & Bug|ahayriski";
        return (void*)info;
    }

    EXPORT void OnModLoad() {
        tw("Loading...", g_wide, 256); 
        pthread_t t;
        pthread_create(&t, nullptr, init_thread, nullptr);
        pthread_detach(t);
    }
}
