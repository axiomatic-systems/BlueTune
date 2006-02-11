#######################################################################
#
#    Module Exports
#
#    (c) 2002-2003 Gilles Boccon-Gibod
#    Author: Gilles Boccon-Gibod (bok@bok.net)
#
#######################################################################

ifeq ($(BLT_PLAYER_MODULE_LINKED),)
BLT_PLAYER_MODULE_LINKED := yes

##########################################################################
# include dependencies
##########################################################################
BLT_LINK_MODULES := Decoder
include $(BLT_BUILD_INCLUDES)/LinkModuleDeps.mak

endif
