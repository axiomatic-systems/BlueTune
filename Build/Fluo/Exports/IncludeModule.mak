#######################################################################
#
#    Fluo Exports
#
#    (c) 2002-2003 Gilles Boccon-Gibod
#    Author: Gilles Boccon-Gibod (bok@bok.net)
#
#######################################################################

ifeq ($(BLT_FLUO_MODULE_INCLUDED),)
BLT_FLUO_MODULE_INCLUDED := yes

##########################################################################
# include dependencies
##########################################################################
BLT_INCLUDE_MODULES = Atomix
include $(BLT_BUILD_INCLUDES)/IncludeModuleDeps.mak

##########################################################################
# exported variables
##########################################################################
BLT_FLUO_INCLUDES = -I$(BLT_SOURCE_ROOT)/Fluo
BLT_INCLUDES_C   += $(BLT_FLUO_INCLUDES)
BLT_INCLUDES_CPP += $(BLT_FLUO_INCLUDES)

endif
