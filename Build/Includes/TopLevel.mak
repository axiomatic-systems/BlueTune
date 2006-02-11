#######################################################################
#
#    BlueTune Top Level Makefile
#
#    (c) 2002-2003 Gilles Boccon-Gibod
#    Author: Gilles Boccon-Gibod (bok@bok.net)
#
#######################################################################

######################################################################
#    checks 
#####################################################################
ifndef BLT_ROOT
$(error Variable BLT_ROOT is not set)
endif

ifndef BLT_TARGET
$(error Variable BLT_TARGET is not set)
endif

ifndef BLT_BUILD_CONFIG
$(error Variable BLT_BUILD_CONFIG is not set)
endif

######################################################################
#    includes
#####################################################################
include $(BLT_ROOT)/Build/Includes/Defs.mak
include $(BLT_BUILD_INCLUDES)/Exports.mak
include $(BLT_BUILD_TARGET_INCLUDES)/Tools.mak
include $(BLT_BUILD_TARGET_INCLUDES)/Config.mak
include $(BLT_BUILD_CONFIG_INCLUDES)/Config.mak
