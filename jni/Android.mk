LOCAL_PATH := $(call my-dir)

# ── Dobby sebagai prebuilt static library ────────────────────────────────────
# Letakkan libdobby.a hasil build di jni/dobby/lib/armeabi-v7a/
include $(CLEAR_VARS)
LOCAL_MODULE            := dobby_prebuilt
LOCAL_SRC_FILES         := dobby/lib/armeabi-v7a/libdobby.a
LOCAL_EXPORT_C_INCLUDES := $(LOCAL_PATH)/dobby/include
include $(PREBUILT_STATIC_LIBRARY)

# ── libnametag.so ─────────────────────────────────────────────────────────────
include $(CLEAR_VARS)
LOCAL_MODULE           := nametag
LOCAL_SRC_FILES        := main.cpp
LOCAL_C_INCLUDES       := $(LOCAL_PATH)/dobby/include
LOCAL_STATIC_LIBRARIES := dobby_prebuilt
LOCAL_LDLIBS           := -llog -lz
LOCAL_CPPFLAGS         := -std=c++17 -O2 -fno-exceptions -fno-rtti \
                          -fvisibility=hidden \
                          -DANDROID -DNDEBUG
LOCAL_ARM_MODE         := arm
# Paksa Thumb untuk kompatibilitas hook
LOCAL_ARM_NEON         := false
include $(BUILD_SHARED_LIBRARY)
