#######################################################################
#
#    Module Exports
#
#    (c) 2002-2003 Gilles Boccon-Gibod
#    Author: Gilles Boccon-Gibod (bok@bok.net)
#
#######################################################################

ifeq ($(BLT_BLUETUNE_MODULE_LINKED),)
BLT_BLUETUNE_MODULE_LINKED := yes

##########################################################################
# include dependencies
##########################################################################
BLT_LINK_MODULES := Player
include $(BLT_BUILD_INCLUDES)/LinkModuleDeps.mak

endif
