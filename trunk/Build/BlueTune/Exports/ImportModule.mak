#######################################################################
#
#    Module Exports
#
#    (c) 2002-2003 Gilles Boccon-Gibod
#    Author: Gilles Boccon-Gibod (bok@bok.net)
#
#######################################################################

ifeq ($(BLT_BLUETUNE_MODULE_IMPORTED),)
BLUETUNE_MODULE_IMPORTED := yes

##########################################################################
# includes
##########################################################################
include $(BLT_BUILD_ROOT)/BlueTune/Exports/LinkModule.mak
include $(BLT_BUILD_ROOT)/BlueTune/Exports/IncludeModule.mak

##########################################################################
# libraries
##########################################################################
ifeq ($(BLT_CLIENT_TYPE),CPP)
BLT_MODULE_LIBRARIES += libBlueTune.a
else
include $(BLT_BUILD_ROOT)/Decoder/Exports/ImportModule.mak
endif

##########################################################################
# module variables
##########################################################################
BLT_BUILD_DIR = $(BLT_ROOT)/Build/BlueTune/Targets/$(BLT_TARGET)

##########################################################################
# targets
##########################################################################
.PHONY: Import-BlueTune
ifeq ($(BLT_CLIENT_TYPE),CPP)
Import-BlueTune: 
	$(BLT_MAKE_BANNER_START)
	$(BLT_SUB_MAKE) -C $(BLT_BUILD_DIR)
	@$(BLT_COPY_IF_NEW) $(BLT_BUILD_DIR)/$(BLT_BUILD_CONFIG)/libBlueTune.a .
	$(BLT_MAKE_BANNER_END)	
else
Import-BlueTune: Import-Decoder
endif

.PHONY: Clean-BlueTune
Clean-BlueTune:
	$(BLT_MAKE_BANNER_START)
	$(BLT_SUB_MAKE) -C $(BLT_BUILD_DIR) clean-deps
	$(BLT_MAKE_BANNER_END)

endif
