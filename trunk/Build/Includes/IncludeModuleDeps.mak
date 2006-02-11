#######################################################################
#
#    Module Dependencies Processing
#
#    (c) 2002-2003 Gilles Boccon-Gibod
#    Author: Gilles Boccon-Gibod (bok@bok.net)
#
#######################################################################
ifneq ($(BLT_INCLUDE_MODULES),)
include $(foreach module,$(BLT_INCLUDE_MODULES),$(BLT_BUILD_MODULES)/Include$(module).mak)
BLT_INCLUDE_MODULES :=
endif

