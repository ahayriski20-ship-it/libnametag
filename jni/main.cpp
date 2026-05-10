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
typedef void (*fn_SP)(unsigned char);
typedef void (*fn_HD)();
typedef void (*fn_DBR)(float,float,float,float,CRGBA*); // DrawBoxRect

// OFFSET DARI READELF LU
#define OFF_PS  0x5AA191u
#define OFF_SC  0x5AAFC9u
#define OFF_SS  0x5AB109u
#define OFF_SO  0x5AB305u
#define OFF_SD  0x5A8A6Du
#define OFF_SF  0x5AB14Du
#define OFF_SE  0x5AB27Du
#define OFF_SP  0x5AB0BDu
#define OFF_HD  0x43A659u 

static fn_PS gPS; static fn_SC gSC; static fn_SS gSS;
static fn_SO gSO; static fn_SD gSD; static fn_SF gSF;
static fn_SE gSE; static fn_SP gSP; static fn_HD gOHD;

static gw g_wide[256]={0};
static bool g_ready=false;

static char g_text[256] = "Riski Boren";
static float g_posX = 320.0f;
static float g_posY = 410.0f; 
static float g_scale = 1.8f; 

static void tw(const char* s, gw* d, int m) {
    int i = 0;
    while (s[i] && i < m - 1) {
        d[i] = (gw)(unsigned char)s[i];
        i++;
    }
    d[i] = 0; // Wajib Null-Terminate biar nggak crash di FilterOutTokens
}

static void load_config() {
    const char* path = "/storage/emulated/0/riski_config.txt";
    FILE* f = fopen(path, "r");
    if (f) {
        char line[256];
        if (fgets(line, sizeof(line), f)) { line[strcspn(line, "\r\n")] = 0; if(strlen(line) > 0) strcpy(g_text, line); }
        if (fgets(line, sizeof(line), f)) sscanf(line, "%f", &g_posX);
        if (fgets(line, sizeof(line), f)) sscanf(line, "%f", &g_posY);
        if (fgets(line, sizeof(line), f)) sscanf(line, "%f", &g_scale);
        fclose(f);
    } else {
        f = fopen(path, "w");
        if (f) { fprintf(f, "%s\n%.1f\n%.1f\n%.1f\n", g_text, g_posX, g_posY, g_scale); fclose(f); }
    }
    tw(g_text, g_wide, 256);
}

static void draw_watermark(){
    if(!gPS || !gSC || !gSS || !g_ready) return;
    
    // Setting Font agar HD (Style 1 biasanya paling jelas untuk teks besar)
    gSF(1); 
    gSP(1); // PROPORTIONAL ON (Anti Burik)
    gSO(1); // CENTER ALIGN
    gSD(0); 
    gSE(0); 
    gSS(g_scale);
    
    // 1. Gambar Background Transparan (Teknik Multiple Shadow biar kaya video)
    CRGBA bgColor = {0, 0, 0, 150};
    gSC(&bgColor);
    // Kita buat 4 lapis shadow tipis biar teksnya punya 'alas' yang jelas
    for(float i=1.0f; i<=3.0f; i+=1.0f) {
        gPS(g_posX+i, g_posY+i, g_wide);
        gPS(g_posX-i, g_posY+i, g_wide);
    }

    // 2. Gambar Teks Utama (Putih Terang)
    CRGBA textColor = {255, 255, 255, 255};
    gSC(&textColor);
    gPS(g_posX, g_posY, g_wide);
}

static void hook_DrawAfterFade(){
    if(gOHD) ((fn_HD)gOHD)();
    draw_watermark();
}

static int find_lib_base(struct dl_phdr_info *info, size_t size, void *data) {
    if (strstr(info->dlpi_name, "libGTASA.so")) {
        *(uintptr_t *)data = info->dlpi_addr;
        return 1;
    }
    return 0;
}

#define T_PTR(a) ((a) | 1u)

static void* init_thread(void*) {
    uintptr_t b = 0;
    while (b == 0) { dl_iterate_phdr(find_lib_base, &b); sleep(1); }
    
    sleep(8); // Nunggu agak lama biar SAMP bener-bener stabil
    load_config();

    void* hDobby = dlopen("libdobby.so", RTLD_NOW | RTLD_GLOBAL);
    if (!hDobby) return nullptr;
    auto dobbyHook = (int(*)(void*,void*,void**)) dlsym(hDobby, "DobbyHook");

    gSC=(fn_SC)T_PTR(b+OFF_SC); gSS=(fn_SS)T_PTR(b+OFF_SS);
    gSO=(fn_SO)T_PTR(b+OFF_SO); gSD=(fn_SD)T_PTR(b+OFF_SD);
    gSF=(fn_SF)T_PTR(b+OFF_SF); gSE=(fn_SE)T_PTR(b+OFF_SE);
    gSP=(fn_SP)T_PTR(b+OFF_SP); gPS=(fn_PS)T_PTR(b+OFF_PS);

    void* target = (void*)T_PTR(b + OFF_HD);
    if (dobbyHook && dobbyHook(target, (void*)hook_DrawAfterFade, (void**)&gOHD) == 0) {
        g_ready = true;
    }
    return nullptr; 
}

extern "C" {
    EXPORT void* __GetModInfo() { static const char* info = "riski|1.1|Jumbo HD Fix|ahayriski"; return (void*)info; }
    EXPORT void OnModLoad() { tw("Loading...", g_wide, 256); pthread_t t; pthread_create(&t, nullptr, init_thread, nullptr); pthread_detach(t); }
}
