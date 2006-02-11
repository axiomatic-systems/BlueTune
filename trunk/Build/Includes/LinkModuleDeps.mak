#######################################################################
#
#    Module Dependencies Processing
#
#    (c) 2002-2003 Gilles Boccon-Gibod
#    Author: Gilles Boccon-Gibod (bok@bok.net)
#
#######################################################################
ifneq ($(BLT_LINK_MODULES),)
include $(foreach module,$(BLT_LINK_MODULES),$(BLT_BUILD_MODULES)/Link$(module).mak)
BLT_LINK_MODULES :=
endif

