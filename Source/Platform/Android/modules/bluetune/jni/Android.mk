LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_MODULE     := bluetune-jni
LOCAL_SRC_FILES  := bluetune-jni.cpp
LOCAL_LDLIBS     += -lOpenSLES
LOCAL_LDLIBS     += -llog
LOCAL_LDLIBS     += -landroid

BLT_ROOT := $(LOCAL_PATH)/../../../../../..
BLT_SRC_ROOT := $(BLT_ROOT)/Source
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

LOCAL_LDLIBS += -lBlueTune
LOCAL_LDLIBS += -lAtomix
LOCAL_LDLIBS += -lNeptune
LOCAL_LDLIBS += -lBento4
ifeq ($(NDK_DEBUG),1) 
LOCAL_LDLIBS += -L$(BLT_ROOT)/Build/Targets/$(TARGET_ARCH)-android-linux/Debug
else
LOCAL_LDLIBS += -L$(BLT_ROOT)/Build/Targets/$(TARGET_ARCH)-android-linux/Release
endif

include $(BUILD_SHARED_LIBRARY)
$(info "----------------------")
$(info $(TARGET_ARCH_ABI) $(TARGET_ARCH))