#include <jni.h>
#include <android/log.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <fcntl.h>
#include <unistd.h>
#include <cstring>
#include <cstdint>
#include <cstdlib>
#include <pthread.h>

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

// OFFSET GTA SA v1.08
#define OFF_PS  0x583D71u
#define OFF_SC  0x582D01u
#define OFF_SS  0x582C9Du
#define OFF_SO  0x582CE5u
#define OFF_SD  0x582D69u
#define OFF_SF  0x582C65u
#define OFF_SE  0x582D8Du
#define OFF_HD  0x58EB55u // CHud::Draw 

static fn_PS gPS; static fn_SC gSC; static fn_SS gSS;
static fn_SO gSO; static fn_SD gSD; static fn_SF gSF;
static fn_SE gSE; static fn_HD gOHD;

// Hardcode teks disini, kita buang variabel g_utf8
static gw g_wide[256]={};
static bool g_ready=false;

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
    return true;
}

// Konverter string biasa ke wide string untuk engine GTA
static void tw(const char*s, gw*d, int m){
    int i=0;
    while(*s && i<m-1) d[i++]=(gw)(unsigned char)*s++;
    d[i]=0;
}

static void draw(){
    if(!gPS || !gSC || !gSS) return;
    const float X=360.0f, Y=45.0f;
    if(gSF) gSF(1); if(gSD) gSD(0);
    if(gSE) gSE(1); gSC(0,0,0,200); gSS(0.5f,1.0f); if(gSO) gSO(0); gPS(X+1.5f, Y+1.5f, g_wide);
    if(gSE) gSE(0); gSC(255,255,255,255); gSS(0.5f,1.0f); if(gSO) gSO(0); gPS(X, Y, g_wide);
}

static void hook_HD(){
    ((fn_HD)gOHD)();
    if(g_ready) draw();
}

static uintptr_t get_base_posix(const char* lib_name) {
    int fd = open("/proc/self/maps", O_RDONLY);
    if (fd < 0) return 0;
    char buf[512]; uintptr_t base_addr = 0; ssize_t bytes_read;
    char line[512]; int line_pos = 0;
    while ((bytes_read = read(fd, buf, sizeof(buf))) > 0) {
        for (ssize_t i = 0; i < bytes_read; i++) {
            if (buf[i] == '\n' || i == bytes_read - 1) {
                line[line_pos] = '\0';
                if (strstr(line, lib_name) && strstr(line, "r-xp")) {
                    base_addr = (uintptr_t)strtoul(line, nullptr, 16);
                    close(fd); return base_addr;
                }
                line_pos = 0;
            } else { if (line_pos < 511) line[line_pos++] = buf[i]; }
        }
    }
    close(fd); return base_addr;
}

#define T_PTR(a) ((a) | 1u)

static void* init_thread(void*) {
    uintptr_t b = 0;
    // Tunggu libGTASA termuat di memori
    while (!(b = get_base_posix("libGTASA.so"))) { sleep(1); }
    
    // Beri jeda 3 detik agar sistem modloader SAMP Alyn selesai me-loading
    // map dan UI server, sehingga tidak tabrakan dengan hook kita
    sleep(3); 

    gSC=(fn_SC)T_PTR(b+(OFF_SC&~1u)); gSS=(fn_SS)T_PTR(b+(OFF_SS&~1u));
    gSO=(fn_SO)T_PTR(b+(OFF_SO&~1u)); gSD=(fn_SD)T_PTR(b+(OFF_SD&~1u));
    gSF=(fn_SF)T_PTR(b+(OFF_SF&~1u)); gSE=(fn_SE)T_PTR(b+(OFF_SE&~1u));
    gPS=(fn_PS)T_PTR(b+(OFF_PS&~1u));
    
    if(thumb_hook(b+(OFF_HD&~1u),(void*)hook_HD,(void**)&gOHD)){
        g_ready = true; 
        LOGI("Watermark siap!");
    }
    
    // Thread selesai bertugas dan langsung mati. Tidak ada lagi infinite loop (while(1))
    // yang memberatkan memori/Scudo Allocator!
    return nullptr;
}

__attribute__((constructor)) static void init(){
    // Setup teksnya sekali saja di awal
    tw("Riski Aja", g_wide, 256);
    
    pthread_t t;
    pthread_create(&t, nullptr, init_thread, nullptr);
    pthread_detach(t);
}
