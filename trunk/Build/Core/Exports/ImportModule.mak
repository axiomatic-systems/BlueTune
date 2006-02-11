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
include $(BLT_BUILD_ROOT)/Core/Exports/LinkModule.mak
include $(BLT_BUILD_ROOT)/Core/Exports/IncludeModule.mak

##########################################################################
# exported variables
##########################################################################
BLT_MODULE_LIBRARIES += libBltCore.a

##########################################################################
# module targets
##########################################################################
BLT_CORE_BUILD_DIR = $(BLT_BUILD_ROOT)/Core/Targets/$(BLT_TARGET)

.PHONY: Import-Core
Import-Core:
	$(BLT_MAKE_BANNER_START)
	$(BLT_SUB_MAKE) -C $(BLT_CORE_BUILD_DIR)
	@$(BLT_COPY_IF_NEW) $(BLT_CORE_BUILD_DIR)/$(BLT_BUILD_CONFIG)/libBltCore.a .
	$(BLT_MAKE_BANNER_END)

.PHONY: Clean-Core
Clean-Core:
	$(BLT_MAKE_BANNER_START)
	$(BLT_SUB_MAKE) -C $(BLT_CORE_BUILD_DIR) clean-deps
	$(BLT_MAKE_BANNER_END)
