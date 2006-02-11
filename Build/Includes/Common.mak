#######################################################################
#
#    Common Target Makefile
#
#    (c) 2002-2003 Gilles Boccon-Gibod
#    Author: Gilles Boccon-Gibod (bok@bok.net)
#
#######################################################################

#######################################################################
#    includes
#######################################################################
include $(BLT_ROOT)/Build/Includes/TopLevel.mak
include $(BLT_BUILD_INCLUDES)/BuildConfig.mak
-include ../Local.mak
include ../../../Makefiles/Module.mak
include $(BLT_BUILD_INCLUDES)/Rules.mak
include $(BLT_BUILD_INCLUDES)/AutoDep.mak
