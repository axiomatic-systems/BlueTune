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
#include $(BLT_BUILD_ROOT)/Player/Exports/LinkModule.mak
include $(BLT_BUILD_ROOT)/Player/Exports/IncludeModule.mak

##########################################################################
# exported variables
##########################################################################
BLT_MODULE_LIBRARIES += libBltPlayer.a

##########################################################################
# module targets
##########################################################################
BLT_PLAYER_BUILD_DIR = $(BLT_BUILD_ROOT)/Player/Targets/$(BLT_TARGET)

.PHONY: Import-Player
Import-Player:
	$(BLT_MAKE_BANNER_START)
	$(BLT_SUB_MAKE) -C $(BLT_PLAYER_BUILD_DIR)
	@$(BLT_COPY_IF_NEW) $(BLT_PLAYER_BUILD_DIR)/$(BLT_BUILD_CONFIG)/libBltPlayer.a .
	$(BLT_MAKE_BANNER_END)

.PHONY: Clean-Player
Clean-Player:
	$(BLT_MAKE_BANNER_START)
	$(BLT_SUB_MAKE) -C $(BLT_PLAYER_BUILD_DIR) clean-deps
	$(BLT_MAKE_BANNER_END)
