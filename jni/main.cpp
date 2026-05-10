#include <jni.h>
#include <android/log.h>
#include <cstring>
#include <cstdint>

// MATIKAN INLINE HOOK MANUAL, KITA PAKAI AML!
#define TAG "libnametag"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, TAG, __VA_ARGS__)

// Definisi Standar AML
#define AML_EXPORT extern "C" __attribute__((visibility("default")))
typedef unsigned short gw;
typedef void (*fn_PS)(float,float,const gw*);
typedef void (*fn_SC)(unsigned char,unsigned char,unsigned char,unsigned char);
typedef void (*fn_SS)(float,float);
typedef void (*fn_SO)(int);
typedef void (*fn_SD)(int);
typedef void (*fn_SF)(int);
typedef void (*fn_SE)(int);
typedef void (*fn_HD)();

// Pointer ke fungsi asli
static fn_PS gPS; static fn_SC gSC; static fn_SS gSS;
static fn_SO gSO; static fn_SD gSD; static fn_SF gSF;
static fn_SE gSE; static fn_HD gOHD;

static gw g_wide[256]={};

static void tw(const char*s, gw*d, int m){
    int i=0;
    while(*s && i<m-1) d[i++]=(gw)(unsigned char)*s++;
    d[i]=0;
}

// Fungsi Draw kita
static void draw_nametag(){
    if(!gPS || !gSC || !gSS) return;
    const float X=360.0f, Y=45.0f;
    
    if(gSF) gSF(1); 
    if(gSD) gSD(0);
    if(gSE) gSE(1); 
    
    gSC(0,0,0,200); gSS(0.5f,1.0f); if(gSO) gSO(0); gPS(X+1.5f, Y+1.5f, g_wide); // Shadow
    if(gSE) gSE(0); 
    gSC(255,255,255,255); gSS(0.5f,1.0f); if(gSO) gSO(0); gPS(X, Y, g_wide); // Teks Putih
}

// Fungsi Hook (Akan ditangani oleh AML)
static void hook_CHud_Draw() {
    // 1. Panggil aslinya (Biar HUD game tergambar)
    if (gOHD) gOHD();
    
    // 2. Gambar nametag kita
    draw_nametag();
}

// =======================================
// STRUKTUR AML (ANDROID MOD LOADER)
// =======================================
// Kamu butuh interface struct dari AML.
// Kita definisikan secara manual disini agar tidak ribet cari header.
struct aml_interface_t {
    bool (*Hook)(void* target_addr, void* replace_func, void** orig_func);
};
static aml_interface_t* aml = nullptr;

// Struktur informasi mod wajib untuk AML
AML_EXPORT void* GetModInfo() {
    static struct {
        const char* name;
        const char* version;
        const char* author;
        const char* description;
    } modinfo = {
        "RiskiNametag", // Nama mod
        "1.0",         // Versi
        "Riski Boren", // Author
        "Menampilkan watermark Riski Aja" // Deskripsi
    };
    return &modinfo;
}

// Dipanggil oleh AML saat mod di-load
AML_EXPORT void OnModLoad(void* aml_ptr) {
    aml = (aml_interface_t*)aml_ptr;
    LOGI("AML berhasil meload RiskiNametag!");
    
    // Set teks permanen
    tw("Riski Aja", g_wide, 256);
}

// Dipanggil oleh AML saat semua library GTASA sudah siap
AML_EXPORT void OnModPreInject(void* aml_ptr, void* gtasa_base) {
    uintptr_t b = (uintptr_t)gtasa_base;
    LOGI("GTASA Base diterima: 0x%08X", (unsigned)b);
    
    // Offset v1.08
    gSC=(fn_SC)(b + 0x582D01u + 1); // +1 untuk Thumb mode
    gSS=(fn_SS)(b + 0x582C9Du + 1);
    gSO=(fn_SO)(b + 0x582CE5u + 1);
    gSD=(fn_SD)(b + 0x582D69u + 1);
    gSF=(fn_SF)(b + 0x582C65u + 1);
    gSE=(fn_SE)(b + 0x582D8Du + 1);
    gPS=(fn_PS)(b + 0x583D71u + 1);
    
    // HOOK MENGGUNAKAN AML API (Sangat Aman!)
    // Target CHud::Draw (0x58EB54 di v1.08. Tidak perlu +1 saat di-hook)
    if(aml && aml->Hook) {
        aml->Hook((void*)(b + 0x58EB54u), (void*)hook_CHud_Draw, (void**)&gOHD);
        LOGI("Hook CHud::Draw via AML sukses!");
    } else {
        LOGE("AML Interface gagal diakses.");
    }
}
