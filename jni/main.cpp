#include <jni.h>
#include <android/log.h>
#include <dlfcn.h>
#include <link.h>
#include <cstring>
#include <cstdint>
#include <pthread.h>
#include <unistd.h>
#include <stdio.h>

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

// OFFSET AKURAT 100%
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

static void tw(const char*s, gw*d, int m){
    int i=0;
    while(*s && i<m-1) d[i++]=(gw)(unsigned char)*s++;
    d[i]=0;
}

// FUNGSI BACA NAMA DARI STORAGE
static void load_name_from_file() {
    char name[256] = "rename name"; // Teks default jika file tidak ada
    
    FILE* f = fopen("/storage/emulated/0/name.txt", "r");
    if (f) {
        char temp[256] = {0};
        if (fgets(temp, sizeof(temp), f)) {
            temp[strcspn(temp, "\r\n")] = 0; // Hapus spasi enter
            if(strlen(temp) > 0) {
                strcpy(name, temp);
            }
        }
        fclose(f);
    }
    
    tw(name, g_wide, 256);
    LOGI("Teks Watermark Terpasang: %s", name);
}

static void draw_watermark(){
    if(!gPS || !gSC || !gSS) return;
    
    // POSISI TENGAH BAWAH (Resolusi Max Virtual GTA: 640x448)
    const float X = 320.0f; // Di tengah
    const float Y = 390.0f; // Di bawah, tidak nabrak tombol
    
    if(gSF) gSF(2); // FONT STYLE 2 (Tebal Khas GTA)
    if(gSD) gSD(0); // Matikan DropShadow bawaan
    if(gSE) gSE(2); // Nyalakan Outline/Tepi Tebal
    if(gSO) gSO(1); // ALIGNMENT: 1 = CENTER (TENGAH)
    
    // Terapkan Teks Utama Putih
    CRGBA text = {255, 255, 255, 255};
    gSC(&text); 
    gSS(1.2f); // SKALA DIBESARKAN (HD)
    
    gPS(X, Y, g_wide);
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

    // Panggil fungsi baca file di sini dengan aman (anti-crash)
    load_name_from_file();

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
        static const char* info = "riski|1.0|Watermark Riski|ahayriski";
        return (void*)info;
    }

    EXPORT void OnModLoad() {
        tw("Loading...", g_wide, 256); 
        pthread_t t;
        pthread_create(&t, nullptr, init_thread, nullptr);
        pthread_detach(t);
    }
}
