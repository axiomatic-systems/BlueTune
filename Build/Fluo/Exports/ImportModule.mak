#######################################################################
#
#    Fluo Exports
#
#    (c) 2002-2003 Gilles Boccon-Gibod
#    Author: Gilles Boccon-Gibod (bok@bok.net)
#
#######################################################################

##########################################################################
# includes
##########################################################################
#include $(BLT_BUILD_ROOT)/Fluo/Exports/LinkModule.mak
include $(BLT_BUILD_ROOT)/Fluo/Exports/IncludeModule.mak

##########################################################################
# exported variables
##########################################################################
BLT_MODULE_LIBRARIES += libFluo.a 

##########################################################################
# module targets
##########################################################################
BLT_FLUO_BUILD_DIR = $(BLT_BUILD_ROOT)/Fluo/Targets/$(BLT_TARGET)

.PHONY: Use-Fluo
Import-Fluo:
	$(BLT_MAKE_BANNER_START)
	$(BLT_SUB_MAKE) -C $(BLT_FLUO_BUILD_DIR)
	@$(BLT_COPY_IF_NEW) $(BLT_FLUO_BUILD_DIR)/$(BLT_BUILD_CONFIG)/libFluo.a .
	$(BLT_MAKE_BANNER_END)

.PHONY: Clean-Fluo
Clean-Fluo:
	$(BLT_MAKE_BANNER_START)
	$(BLT_SUB_MAKE) -C $(BLT_FLUO_BUILD_DIR) clean-deps
	$(BLT_MAKE_BANNER_END)
