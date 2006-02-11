#######################################################################
#
#    Module Exports
#
#    (c) 2002-2003 Gilles Boccon-Gibod
#    Author: Gilles Boccon-Gibod (bok@bok.net)
#
#######################################################################

ifeq ($(BLT_DECODER_MODULE_INCLUDED),)
BLT_DECODER_MODULE_INCLUDED := yes

##########################################################################
# dependencies
##########################################################################
BLT_INCLUDE_MODULES := Core Plugins
include $(BLT_BUILD_INCLUDES)/IncludeModuleDeps.mak

##########################################################################
# exported variables
##########################################################################
BLT_DECODER_INCLUDES = -I$(BLT_SOURCE_ROOT)/Decoder
BLT_INCLUDES_C   += $(BLT_DECODER_INCLUDES)
BLT_INCLUDES_CPP += $(BLT_DECODER_INCLUDES)

endif
