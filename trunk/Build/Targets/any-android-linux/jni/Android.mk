########################## Build Configuration
bluetune_modules = FhgAacDecoder

########################## Shared Variables
LOCAL_PATH := $(call my-dir)
bluetune_root := $(LOCAL_PATH)/../../../..

########################## Atomix
include $(CLEAR_VARS)

atomix_root     := $(bluetune_root)/../Atomix
atomix_sources  := $(wildcard $(atomix_root)/Source/Core/*.c) $(wildcard$(atomix_root)/Source/System/Bsd/*.c)
LOCAL_MODULE    := atomix
LOCAL_SRC_FILES := $(atomix_sources:$(LOCAL_PATH)/%=%) 
LOCAL_CFLAGS    += -DATX_CONFIG_ENABLE_LOGGING

include $(BUILD_STATIC_LIBRARY)

########################## Neptune
include $(CLEAR_VARS)

neptune_root     := $(bluetune_root)/../Neptune
neptune_sources  := $(wildcard $(neptune_root)/Source/Core/*.cpp)
LOCAL_MODULE     := neptune
LOCAL_SRC_FILES  := $(neptune_sources:$(LOCAL_PATH)/%=%) 
LOCAL_CPPFLAGS   += -DNPT_CONFIG_ENABLE_LOGGING

include $(BUILD_STATIC_LIBRARY)

########################## Bento4
include $(CLEAR_VARS)

bento4_root      := $(bluetune_root)/../Bento4
bento4_dirs      := Core MetaData Codecs Crypto
bento4_sources   := $(foreach dir,$(bento4_dirs),$(wildcard $(bento4_root)/Source/C++/$(dir)/*.cpp))
LOCAL_MODULE     := bento4
LOCAL_SRC_FILES  := $(bento4_sources:$(LOCAL_PATH)/%=%) 
LOCAL_CPPFLAGS   += -DNPT_CONFIG_ENABLE_LOGGING
LOCAL_C_INCLUDES := $(foreach dir,$(bento4_dirs),$(bento4_root)/Source/C++/$(dir))

include $(BUILD_STATIC_LIBRARY)

########################## FhgAacDecoder
ifneq ($(filter FhgAacDecoder,$(bluetune_modules)),)
	include $(CLEAR_VARS)

	fhg_aac_decoder_root    := $(bluetune_root)/ThirdParty/FhgAAC
    fhg_aac_decoder_dirs    := libAACdec libFDK libMpegTPDec libPCMutils libSBRdec libSYS
	fhg_aac_decoder_sources := $(foreach dir,$(fhg_aac_decoder_dirs),$(wildcard $(fhg_aac_decoder_root)/$(dir)/src/*.cpp))
	LOCAL_MODULE     := fhg_aac_decoder
	LOCAL_SRC_FILES  := $(fhg_aac_decoder_sources:$(LOCAL_PATH)/%=%) 
	LOCAL_C_INCLUDES := $(foreach dir,$(fhg_aac_decoder_dirs),$(fhg_aac_decoder_root)/$(dir)/include)

	include $(BUILD_STATIC_LIBRARY)
endif