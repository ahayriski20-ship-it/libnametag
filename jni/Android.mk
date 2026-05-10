LOCAL_PATH := $(call my-dir)
include $(CLEAR_VARS)
LOCAL_MODULE    := nametag
LOCAL_SRC_FILES := main.cpp
LOCAL_LDLIBS    := -llog
LOCAL_CPPFLAGS  := -std=c++17 -O2 -fno-exceptions -fno-rtti
include $(BUILD_SHARED_LIBRARY)
