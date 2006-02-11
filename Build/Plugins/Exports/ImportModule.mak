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
include $(BLT_BUILD_ROOT)/Plugins/Exports/LinkModule.mak
include $(BLT_BUILD_ROOT)/Plugins/Exports/IncludeModule.mak

##########################################################################
# exported variables
##########################################################################
BLT_MODULE_LIBRARIES += libBltPlugins.a

##########################################################################
# module targets
##########################################################################
BLT_PLUGINS_BUILD_DIR = $(BLT_BUILD_ROOT)/Plugins/Targets/$(BLT_TARGET)

.PHONY: Import-Plugins
Import-Plugins:
	$(BLT_MAKE_BANNER_START)
	$(BLT_SUB_MAKE) -C $(BLT_PLUGINS_BUILD_DIR)
	@$(BLT_COPY_IF_NEW) $(BLT_PLUGINS_BUILD_DIR)/$(BLT_BUILD_CONFIG)/libBltPlugins.a .
	$(BLT_MAKE_BANNER_END)

.PHONY: Clean-Plugins
Clean-Plugins:
	$(BLT_MAKE_BANNER_START)
	$(BLT_SUB_MAKE) -C $(BLT_PLUGINS_BUILD_DIR) clean-deps
	$(BLT_MAKE_BANNER_END)
