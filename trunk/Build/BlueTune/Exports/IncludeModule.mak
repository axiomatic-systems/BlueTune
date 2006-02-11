#######################################################################
#
#    Module Exports
#
#    (c) 2002-2003 Gilles Boccon-Gibod
#    Author: Gilles Boccon-Gibod (bok@bok.net)
#
#######################################################################

ifeq ($(BLT_BLUETUNE_MODULE_INCLUDED),)
BLT_BLUETUNE_MODULE_INCLUDED := yes

##########################################################################
# include dependencies
##########################################################################
BLT_INCLUDE_MODULES := Atomix Player
include $(BLT_BUILD_INCLUDES)/IncludeModuleDeps.mak

##########################################################################
# exported variables
##########################################################################
BLT_BLUETUNE_INCLUDES = -I$(BLT_SOURCE_ROOT)/BlueTune
BLT_INCLUDES_C   += $(BLT_BLUETUNE_INCLUDES)
BLT_INCLUDES_CPP += $(BLT_BLUETUNE_INCLUDES)

endif
