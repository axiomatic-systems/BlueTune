#######################################################################
#
#    Generic Makefiles Rules
#
#    (c) 2002-2003 Gilles Boccon-Gibod
#    Author: Gilles Boccon-Gibod (bok@bok.net)
#
#######################################################################

#######################################################################
#  check configuration variables
#######################################################################
ifndef BLT_TARGET
ifdef MAKECMDGOALS
$(error "BLT_TARGET variable is not set")
endif
endif

ifndef BLT_BUILD_CONFIG
# default build configuration
BLT_BUILD_CONFIG=Debug
endif

#######################################################################
#  target templates
#######################################################################
BLT_ALL_BUILDS = $(BLT_SUBDIR_BUILDS) $(BLT_SUBTARGET_BUILDS)
All: $(BLT_ALL_BUILDS)
Clean-All: $(foreach goal,$(BLT_ALL_BUILDS),Clean-$(goal))

BLT_SUBDIR_CLEANS = $(foreach dir,$(BLT_SUBDIR_BUILDS),Clean-$(dir))
BLT_SUBTARGET_CLEANS = $(foreach dir,$(BLT_SUBTARGET_BUILDS),Clean-$(dir))

.PHONY: $(BLT_SUBDIR_BUILDS)
$(BLT_SUBDIR_BUILDS): 
	$(BLT_MAKE_BANNER_START)
	$(BLT_SUB_MAKE) -C $@ All
	$(BLT_MAKE_BANNER_END)

.PHONY: $(BLT_SUBTARGET_BUILDS)
$(BLT_SUBTARGET_BUILDS): 
	$(BLT_MAKE_BANNER_START)
	$(BLT_SUB_MAKE) -C $@/Targets/$(BLT_TARGET)
	$(BLT_MAKE_BANNER_END)

.PHONY: $(BLT_SUBDIR_CLEANS)
$(BLT_SUBDIR_CLEANS): 
	$(BLT_MAKE_BANNER_START)
	$(BLT_SUB_MAKE) -C $(subst Clean-,,$@) Clean-All
	$(BLT_MAKE_BANNER_END)

.PHONY: $(BLT_SUBTARGET_CLEANS)
$(BLT_SUBTARGET_CLEANS): 
	$(BLT_MAKE_BANNER_START)
	$(BLT_SUB_MAKE) -C $(subst Clean-,,$@)/Targets/$(BLT_TARGET) clean-deps
	$(BLT_MAKE_BANNER_END)
