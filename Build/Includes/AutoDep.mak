#######################################################################
#
#    BlueTune Build Definitions for Automatic Dependencies
#
#    (c) 2002-2003 Gilles Boccon-Gibod
#    Author: Gilles Boccon-Gibod (bok@bok.net)
#
#######################################################################

##########################################################################
# rules
##########################################################################

### autodependency for .cpp files
ifneq ($(BLT_AUTODEP_STDOUT),)
%.d: %.cpp
	$(BLT_AUTODEP_CPP) $(BLT_DEFINES_CPP) $(BLT_INCLUDES_CPP) $< > $@
else
%.d: %.cpp
	$(BLT_AUTODEP_CPP) $(BLT_DEFINES_CPP) $(BLT_INCLUDES_CPP) $< -o $@
endif

### autodependency for .c files
ifneq ($(BLT_AUTODEP_STDOUT),)
%.d: %.c
	$(BLT_AUTODEP_C) $(BLT_DEFINES_C) $(BLT_INCLUDES_C) $< > $@
else
%.d: %.c
	$(BLT_AUTODEP_C) $(BLT_DEFINES_C) $(BLT_INCLUDES_C) $< -o $@
endif

##########################################################################
# auto dependencies
##########################################################################
BLT_MODULE_DEPENDENCIES := $(patsubst %.c,%.d,$(BLT_MODULE_SOURCES))
BLT_MODULE_DEPENDENCIES := $(patsubst %.cpp,%.d,$(BLT_MODULE_DEPENDENCIES))

ifneq ($(MAKECMDGOALS), clean)
ifneq ($(MAKECMDGOALS), clean-deps)
ifdef BLT_MODULE_DEPENDENCIES
include $(BLT_MODULE_DEPENDENCIES)
endif
ifdef BLT_MODULE_LOCAL_RULES
include $(BLT_MODULE_LOCAL_RULES)
endif
endif
endif
