#######################################################################
#
#    Module Dependencies Processing
#
#    (c) 2002-2003 Gilles Boccon-Gibod
#    Author: Gilles Boccon-Gibod (bok@bok.net)
#
#######################################################################
ifneq ($(BLT_IMPORT_MODULES),)
BLT_IMPORTED_MODULE_LIBS  := $(foreach module,$(BLT_IMPORT_MODULES),Import-$(module))
BLT_IMPORTED_MODULE_CLEANS := $(foreach module,$(BLT_IMPORT_MODULES),Clean-$(module))
include $(foreach module,$(BLT_IMPORT_MODULES),$(BLT_BUILD_MODULES)/Import$(module).mak)
BLT_IMPORT_MODULES :=
endif
