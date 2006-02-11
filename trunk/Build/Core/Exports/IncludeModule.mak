#######################################################################
#
#    Module Exports
#
#    (c) 2002-2003 Gilles Boccon-Gibod
#    Author: Gilles Boccon-Gibod (bok@bok.net)
#
#######################################################################

ifeq ($(BLT_CORE_MODULE_INCLUDED),)
BLT_CORE_MODULE_INCLUDED=yes

##########################################################################
# include dependencies
##########################################################################
BLT_INCLUDE_MODULES := Atomix
include $(BLT_BUILD_INCLUDES)/IncludeModuleDeps.mak

##########################################################################
# exported variables
##########################################################################
BLT_CORE_INCLUDES = -I$(BLT_SOURCE_ROOT)/Core -I$(BLT_SOURCE_ROOT)/Config
BLT_INCLUDES_C   += $(BLT_CORE_INCLUDES)
BLT_INCLUDES_CPP += $(BLT_CORE_INCLUDES)

endif
