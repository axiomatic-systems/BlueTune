#######################################################################
#
#    Module Exports
#
#    (c) 2002-2003 Gilles Boccon-Gibod
#    Author: Gilles Boccon-Gibod (bok@bok.net)
#
#######################################################################

ifeq ($(BLT_PLAYER_MODULE_INCLUDED),)
BLT_PLAYER_MODULE_INCLUDED := yes

##########################################################################
# dependencies
##########################################################################
BLT_INCLUDE_MODULES := Decoder
include $(BLT_BUILD_INCLUDES)/IncludeModuleDeps.mak

##########################################################################
# exported variables
##########################################################################
BLT_PLAYER_INCLUDES = -I$(BLT_SOURCE_ROOT)/Player
BLT_INCLUDES_C   += $(BLT_PLAYER_INCLUDES)
BLT_INCLUDES_CPP += $(BLT_PLAYER_INCLUDES)

endif
