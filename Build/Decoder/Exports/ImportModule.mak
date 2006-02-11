#######################################################################
#
#    Module Exports
#
#    (c) 2002-2003 Gilles Boccon-Gibod
#    Author: Gilles Boccon-Gibod (bok@bok.net)
#
#######################################################################

##########################################################################
# includes
##########################################################################
include $(BLT_BUILD_ROOT)/Decoder/Exports/LinkModule.mak
include $(BLT_BUILD_ROOT)/Decoder/Exports/IncludeModule.mak

##########################################################################
# exported variables
##########################################################################
BLT_MODULE_LIBRARIES += libBltDecoder.a

##########################################################################
# module targets
##########################################################################
BLT_DECODER_BUILD_DIR = $(BLT_BUILD_ROOT)/Decoder/Targets/$(BLT_TARGET)

.PHONY: Import-Decoder
Import-Decoder:
	$(BLT_MAKE_BANNER_START)
	$(BLT_SUB_MAKE) -C $(BLT_DECODER_BUILD_DIR)
	@$(BLT_COPY_IF_NEW) $(BLT_DECODER_BUILD_DIR)/$(BLT_BUILD_CONFIG)/libBltDecoder.a .
	$(BLT_MAKE_BANNER_END)

.PHONY: Clean-Decoder
Clean-Decoder:
	$(BLT_MAKE_BANNER_START)
	$(BLT_SUB_MAKE) -C $(BLT_DECODER_BUILD_DIR) clean-deps
	$(BLT_MAKE_BANNER_END)
