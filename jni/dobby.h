#pragma once
/**
 * dobby.h — stub header untuk Dobby hooking framework
 *
 * Dobby prebuilt (.a) perlu didownload dari:
 *   https://github.com/jmpews/Dobby/releases
 * atau build sendiri untuk armeabi-v7a.
 *
 * Letakkan:
 *   jni/dobby/include/dobby.h     ← file ini (atau yang asli)
 *   jni/dobby/lib/armeabi-v7a/libdobby.a
 */

#ifdef __cplusplus
extern "C" {
#endif

/**
 * DobbyHook — pasang inline hook
 * @param address        Alamat fungsi target (Thumb: address | 1)
 * @param replace        Fungsi pengganti kita
 * @param origin_call    Output: pointer ke trampolin fungsi asli
 * @return 0 = sukses
 */
int DobbyHook(void* address, void* replace, void** origin_call);

/**
 * DobbyDestroy — lepas hook
 */
int DobbyDestroy(void* address);

#ifdef __cplusplus
}
#endif
