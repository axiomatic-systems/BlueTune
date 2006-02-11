#######################################################################
#
#    Bootstrap Makefile
#
#    (c) 2002-2003 Gilles Boccon-Gibod
#    Author: Gilles Boccon-Gibod (bok@bok.net)
#
#######################################################################

ifndef BLT_BUILD_CONFIG
BLT_BUILD_CONFIG=Debug
endif

export BLT_ROOT
export BLT_TARGET 
export BLT_BUILD_CONFIG
 
ifndef MAKECMDGOALS
MAKECMDGOALS = $(BLT_DEFAULT_GOAL)
endif
 
$(MAKECMDGOALS):
	@[ -d $(BLT_BUILD_CONFIG) ] || mkdir $(BLT_BUILD_CONFIG)
	@$(MAKE) --no-print-directory -C $(BLT_BUILD_CONFIG) -f $(BLT_ROOT)/Build/Includes/Common.mak $(MAKECMDGOALS)
