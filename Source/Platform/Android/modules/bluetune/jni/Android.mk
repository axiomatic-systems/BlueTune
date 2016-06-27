LOCAL_PATH := $(call my-dir)

BLT_RELATIVE_ROOT = ../../../../../..
BLT_ROOT := $(LOCAL_PATH)/$(BLT_RELATIVE_ROOT)
BLT_SRC_ROOT := $(BLT_ROOT)/Source

ifeq ($(NDK_DEBUG),1) 
BLUETUNE_LIB_ROOT := $(BLT_RELATIVE_ROOT)/Build/Targets/$(TARGET_ARCH)-android-linux/Debug
else
BLUETUNE_LIB_ROOT := $(BLT_RELATIVE_ROOT)/Build/Targets/$(TARGET_ARCH)-android-linux/Release
endif

# BlueTune static lib
include $(CLEAR_VARS)
LOCAL_MODULE          := BlueTuneStaticLib
LOCAL_SRC_FILES := $(BLUETUNE_LIB_ROOT)/libBlueTune.a
include $(PREBUILT_STATIC_LIBRARY)

# Atomix static lib
include $(CLEAR_VARS)
LOCAL_MODULE          := AtomixStaticLib
LOCAL_SRC_FILES := $(BLUETUNE_LIB_ROOT)/libAtomix.a
include $(PREBUILT_STATIC_LIBRARY)

# Neptune static lib
include $(CLEAR_VARS)
LOCAL_MODULE          := NeptuneStaticLib
LOCAL_SRC_FILES := $(BLUETUNE_LIB_ROOT)/libNeptune.a
include $(PREBUILT_STATIC_LIBRARY)

# Bento4 static lib
include $(CLEAR_VARS)
LOCAL_MODULE          := Bento4StaticLib
LOCAL_SRC_FILES := $(BLUETUNE_LIB_ROOT)/libBento4.a
include $(PREBUILT_STATIC_LIBRARY)

# JNI module
include $(CLEAR_VARS)

LOCAL_MODULE     := bluetune-jni
LOCAL_SRC_FILES  := bluetune-jni.cpp
LOCAL_LDLIBS     += -lOpenSLES
LOCAL_LDLIBS     += -llog
LOCAL_LDLIBS     += -landroid

LOCAL_C_INCLUDES += $(BLT_SRC_ROOT)/BlueTune
LOCAL_C_INCLUDES += $(BLT_SRC_ROOT)/Core
LOCAL_C_INCLUDES += $(BLT_SRC_ROOT)/Player
LOCAL_C_INCLUDES += $(BLT_SRC_ROOT)/Decoder
LOCAL_C_INCLUDES += $(BLT_SRC_ROOT)/Plugins/Common
LOCAL_C_INCLUDES += $(BLT_SRC_ROOT)/Plugins/DynamicLoading
LOCAL_C_INCLUDES += $(BLT_ROOT)/../Neptune/Source/Core
LOCAL_C_INCLUDES += $(BLT_ROOT)/../Atomix/Source/Core

LOCAL_CFLAGS += -DATX_CONFIG_ENABLE_LOGGING
LOCAL_CFLAGS += -DNPT_CONFIG_ENABLE_LOGGING

LOCAL_STATIC_LIBRARIES = BlueTuneStaticLib AtomixStaticLib NeptuneStaticLib Bento4StaticLib

include $(BUILD_SHARED_LIBRARY)
$(info "----------------------")
$(info $(TARGET_ARCH_ABI) $(TARGET_ARCH))