#include <jni.h>
#include <android/log.h>
#include <sys/mman.h>
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <unistd.h>
#include <cstdlib>

#define TAG "libnametag"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, TAG, __VA_ARGS__)

typedef unsigned short gw;
typedef void (*fn_PS)(float,float,const gw*);
typedef void (*fn_SC)(unsigned char,unsigned char,unsigned char,unsigned char);
typedef void (*fn_SS)(float,float);
typedef void (*fn_SO)(int);
typedef void (*fn_SD)(int);
typedef void (*fn_SF)(int);
typedef void (*fn_SE)(int);
typedef void (*fn_HD)();

#define OFF_PS  0x5AA191u
#define OFF_SC  0x5A8C11u
#define OFF_SS  0x5A8B91u
#define OFF_SO  0x5A8BD9u
#define OFF_SD  0x5A8C51u
#define OFF_SF  0x5A8B51u
#define OFF_SE  0x5A8C71u
#define OFF_HD  0x58D591u 

static fn_PS gPS; static fn_SC gSC; static fn_SS gSS;
static fn_SO gSO; static fn_SD gSD; static fn_SF gSF;
static fn_SE gSE; static fn_HD gOHD;

static char g_utf8[256]="PlayerName";
static gw g_wide[256]={};
static bool g_ready=false;
static int g_frameCount = 0; // rate limiter untuk reload nama

static const char* kP[]={
    "/storage/emulated/0/name.txt",
    "/sdcard/name.txt",
    "/storage/emulated/0/SAMP/name.txt",
    "/storage/emulated/0/Android/data/com.sampmobilerp.game/mods/name.txt",
    "/storage/emulated/0/Android/data/ro.alyn_sampmobile.game/mods/name.txt",
    nullptr
};

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
    LOGI("hook OK addr=0x%08X rep=0x%08X", (unsigned)addr, (unsigned)rep);
    return true;
}

static void tw(const char*s,gw*d,int m){
    int i=0;
    while(*s && i<m-1) d[i++]=(gw)(unsigned char)*s++;
    d[i]=0;
}

static void load(){
    for(int i=0; kP[i]; i++){
        FILE* f = fopen(kP[i], "r");
        if(!f) continue;
        char b[256] = {0};
        bool ok = fgets(b, sizeof(b), f) != nullptr;
        fclose(f);
        if(!ok) continue;
        int n = strlen(b);
        while(n>0 && (b[n-1]=='\n' || b[n-1]=='\r')) b[--n]=0;
        if(!n) continue;
        
        strncpy(g_utf8, b, 255);
        tw(g_utf8, g_wide, 256);
        return;
    }
    tw(g_utf8, g_wide, 256);
}

static void draw(){
    if(!gPS || !gSC || !gSS) return;
    const float X=360.0f, Y=45.0f;
    if(gSF) gSF(1); if(gSD) gSD(0);
    if(gSE) gSE(1); gSC(0,0,0,200); gSS(0.5f,1.0f); if(gSO) gSO(0); gPS(X+1.5f, Y+1.5f, g_wide);
    if(gSE) gSE(0); gSC(255,255,255,255); gSS(0.5f,1.0f); if(gSO) gSO(0); gPS(X, Y, g_wide);
}

// Hook dijalankan di thread utama game, sangat aman dari memory crash
static void hook_HD(){
    ((fn_HD)gOHD)();
    if(g_ready){
        draw();
        // Auto-reload nama setiap 120 frame (~2 detik di 60 FPS) agar tidak spam file system
        g_frameCount++;
        if (g_frameCount >= 120) {
            load();
            g_frameCount = 0;
        }
    }
}

static uintptr_t get_base(const char*lib){
    char p[64];snprintf(p,64,"/proc/%d/maps",getpid());
    FILE*f=fopen(p,"r");if(!f)return 0;char l[512];uintptr_t b=0;
    while(fgets(l,512,f))if(strstr(l,lib)&&strstr(l,"r-xp")){b=(uintptr_t)strtoul(l,nullptr,16);break;}
    fclose(f);return b;
}

#define T_PTR(a) ((a) | 1u)

__attribute__((constructor)) static void init(){
    LOGI("init nametag");
    load(); // Baca saat awal
    
    // Alih-alih pakai thread yang menyebabkan crash, kita pasang logic untuk menunggu libGTASA.so
    // tanpa membekukan game (biasanya constructor dijalankan sesaat setelah load)
    uintptr_t b = get_base("libGTASA.so");
    if (!b) {
        LOGI("libGTASA not found during constructor, trying fallback method.");
        // Berikan delay sangat singkat jika libGTASA telat dimuat
        for (int i=0; i<10; i++) {
            usleep(500000); // 0.5 detik * 10 = 5 detik maksimal menunggu
            b = get_base("libGTASA.so");
            if (b) break;
        }
    }
    
    if (b) {
        LOGI("base=0x%08X", (unsigned)b);
        gSC=(fn_SC)T_PTR(b+(OFF_SC&~1u)); gSS=(fn_SS)T_PTR(b+(OFF_SS&~1u));
        gSO=(fn_SO)T_PTR(b+(OFF_SO&~1u)); gSD=(fn_SD)T_PTR(b+(OFF_SD&~1u));
        gSF=(fn_SF)T_PTR(b+(OFF_SF&~1u)); gSE=(fn_SE)T_PTR(b+(OFF_SE&~1u));
        gPS=(fn_PS)T_PTR(b+(OFF_PS&~1u));
        
        if(thumb_hook(b+(OFF_HD&~1u),(void*)hook_HD,(void**)&gOHD)){
            g_ready=true; 
            LOGI("ready name=%s",g_utf8);
        } else {
            LOGE("hook fail");
        }
    } else {
        LOGE("libGTASA completely missing.");
    }
}
