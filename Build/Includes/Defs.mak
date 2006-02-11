#######################################################################
#
#    BlueTune Build Definitions
#
#    (c) 2002-2003 Gilles Boccon-Gibod
#    Author: Gilles Boccon-Gibod (bok@bok.net)
#
#######################################################################

#######################################################################
# variables
#######################################################################
XXX_CLIENT                = BLT
BLT_BUILD_ROOT            = $(BLT_ROOT)/Build
BLT_BUILD_INCLUDES        = $(BLT_BUILD_ROOT)/Includes
BLT_BUILD_MODULES         = $(BLT_BUILD_INCLUDES)/Modules
BLT_BUILD_TARGET_INCLUDES = $(BLT_BUILD_INCLUDES)/Targets/$(BLT_TARGET)
BLT_BUILD_CONFIG_INCLUDES = $(BLT_BUILD_TARGET_INCLUDES)/BuildConfigs/$(BLT_BUILD_CONFIG)
BLT_SOURCE_ROOT           = $(BLT_ROOT)/Source
BLT_THIRDPARTY_ROOT       = $(BLT_ROOT)/ThirdParty
ifndef BLT_BUILD_CONFIG
BLT_BUILD_CONFIG = Debug
endif
