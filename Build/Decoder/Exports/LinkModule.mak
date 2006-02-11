#######################################################################
#
#    Module Exports
#
#    (c) 2002-2003 Gilles Boccon-Gibod
#    Author: Gilles Boccon-Gibod (bok@bok.net)
#
#######################################################################

ifeq ($(BLT_DECODER_MODULE_LINKED),)
BLT_DECODER_MODULE_LINKED := yes

##########################################################################
# dependencies
##########################################################################
BLT_LINK_MODULES := Core Plugins
include $(BLT_BUILD_INCLUDES)/LinkModuleDeps.mak

endif
