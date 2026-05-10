#include <jni.h>
#include <android/log.h>
#include <sys/mman.h>
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <unistd.h>
#include <pthread.h>

#define TAG "libnametag"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  TAG, __VA_ARGS__)
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
#define OFF_HD  0x58E919u

static fn_PS gPS; static fn_SC gSC; static fn_SS gSS;
static fn_SO gSO; static fn_SD gSD; static fn_SF gSF;
static fn_SE gSE; static fn_HD gOHD;

static char g_utf8[256]="PlayerName";
static gw   g_wide[256]={};
static bool g_ready=false;
static pthread_mutex_t g_mtx=PTHREAD_MUTEX_INITIALIZER;

// Penambahan path ro.alyn_sampmobile.game/mods
static const char* kP[]={
    "/storage/emulated/0/name.txt",
    "/sdcard/name.txt",
    "/storage/emulated/0/SAMP/name.txt",
    "/storage/emulated/0/Android/data/com.sampmobilerp.game/mods/name.txt",
    "/storage/emulated/0/Android/data/ro.alyn_sampmobile.game/mods/name.txt",
    nullptr
};

// ── Inline hook tanpa Dobby ───────────────────────────────────
// Trampoline buffer: 8 byte asli + 8 byte jump back = 16 byte
static uint8_t s_tramp[16] __attribute__((aligned(4)));

static bool thumb_hook(uintptr_t addr, void* replace, void** orig) {
    // addr = raw address (bit0 clear), fungsi Thumb
    int ps = getpagesize();
    // Buat halaman target writable
    void* page = (void*)(addr & ~(uintptr_t)(ps-1));
    if (mprotect(page, ps*2, PROT_READ|PROT_WRITE|PROT_EXEC) != 0) {
        LOGE("mprotect target failed"); return false; }
    // Simpan 8 byte asli ke trampoline
    memcpy(s_tramp, (void*)addr, 8);
    // Tambah jump back ke addr+8 (Thumb: |1)
    uintptr_t ret = addr + 8;
    // LDR.W PC, [PC, #0]  encoding Thumb2 LE: DF F8 00 F0
    s_tramp[8]=0xDF; s_tramp[9]=0xF8; s_tramp[10]=0x00; s_tramp[11]=0xF0;
    memcpy(s_tramp+12, &ret, 4);
    // Buat trampoline executable
    void* tp = (void*)((uintptr_t)s_tramp & ~(uintptr_t)(ps-1));
    mprotect(tp, ps, PROT_READ|PROT_WRITE|PROT_EXEC);
    __builtin___clear_cache((char*)s_tramp, (char*)s_tramp+16);
    // Tulis hook: LDR.W PC, [PC, #0] + replace addr
    uintptr_t rep = (uintptr_t)replace;
    uint8_t patch[8]={0xDF,0xF8,0x00,0xF0};
    memcpy(patch+4, &rep, 4);
    memcpy((void*)addr, patch, 8);
    __builtin___clear_cache((char*)addr, (char*)addr+8);
    *orig = (void*)((uintptr_t)s_tramp | 1); // Thumb trampoline
    LOGI("hook OK addr=0x%08X rep=0x%08X", (unsigned)addr, (unsigned)rep);
    return true;
}

static void tw(const char*s,gw*d,int m){int i=0;while(*s&&i<m-1)d[i++]=(gw)(unsigned char)*s++;d[i]=0;}
static void load(){
    for(int i=0;kP[i];i++){FILE*f=fopen(kP[i],"r");if(!f)continue;
    char b[256]={};bool ok=fgets(b,256,f)!=nullptr;fclose(f);if(!ok)continue;
    int n=strlen(b);while(n>0&&(b[n-1]=='\n'||b[n-1]=='\r'))b[--n]=0;if(!n)continue;
    pthread_mutex_lock(&g_mtx);strncpy(g_utf8,b,255);tw(g_utf8,g_wide,256);
    pthread_mutex_unlock(&g_mtx);LOGI("name=%s",g_utf8);return;}
    pthread_mutex_lock(&g_mtx);tw(g_utf8,g_wide,256);pthread_mutex_unlock(&g_mtx);}

static void* watcher(void*){while(1){sleep(3);load();}return nullptr;}

static void draw(){
    if(!gPS||!gSC||!gSS)return;
    gw buf[256];pthread_mutex_lock(&g_mtx);memcpy(buf,g_wide,sizeof(g_wide));pthread_mutex_unlock(&g_mtx);
    const float X=360.0f,Y=45.0f;
    if(gSF)gSF(1);if(gSD)gSD(0);
    if(gSE)gSE(1);gSC(0,0,0,200);gSS(0.5f,1.0f);if(gSO)gSO(0);gPS(X+1.5f,Y+1.5f,buf);
    if(gSE)gSE(0);gSC(255,255,255,255);gSS(0.5f,1.0f);if(gSO)gSO(0);gPS(X,Y,buf);}

static void hook_HD(){((fn_HD)gOHD)();if(g_ready)draw();}

static uintptr_t get_base(const char*lib){
    char p[64];snprintf(p,64,"/proc/%d/maps",getpid());
    FILE*f=fopen(p,"r");if(!f)return 0;char l[512];uintptr_t b=0;
    while(fgets(l,512,f))if(strstr(l,lib)&&strstr(l,"r-xp")){b=(uintptr_t)strtoul(l,nullptr,16);break;}
    fclose(f);return b;}

static inline uintptr_t R(uintptr_t a){return a&~1u;}

__attribute__((constructor)) static void init(){
    LOGI("init"); load();
    uintptr_t b=get_base("libGTASA.so");
    if(!b){LOGE("libGTASA not found");return;}
    LOGI("base=0x%08X",(unsigned)b);
    gSC=(fn_SC)R(b+(OFF_SC&~1u));gSS=(fn_SS)R(b+(OFF_SS&~1u));
    gSO=(fn_SO)R(b+(OFF_SO&~1u));gSD=(fn_SD)R(b+(OFF_SD&~1u));
    gSF=(fn_SF)R(b+(OFF_SF&~1u));gSE=(fn_SE)R(b+(OFF_SE&~1u));
    gPS=(fn_PS)R(b+(OFF_PS&~1u));
    if(!thumb_hook(b+(OFF_HD&~1u),(void*)hook_HD,(void**)&gOHD)){LOGE("hook fail");return;}
    pthread_t t;pthread_create(&t,nullptr,watcher,nullptr);pthread_detach(t);
    g_ready=true;LOGI("ready name=%s",g_utf8);}
