#######################################################################
#
#    BlueTune Common Makefile Rules
#
#    (c) 2002-2003 Gilles Boccon-Gibod
#    Author: Gilles Boccon-Gibod (bok@bok.net)
#
#######################################################################

##########################################################################
#    pattern rules
##########################################################################

### build .o from .c
%.o: %.c
	$(BLT_COMPILE_C) $(BLT_FLAGS_C) $(BLT_DEFINES_C) $(BLT_INCLUDES_C) $(BLT_OPTIMIZE_C) $(BLT_DEBUG_C) $(BLT_PROFILE_C) $(BLT_WARNINGS_C) $(BLT_LOCAL_FLAGS) -c $< -o $@

### build .o from .cpp
%.o: %.cpp
	$(BLT_COMPILE_CPP) $(BLT_FLAGS_CPP) $(BLT_DEFINES_CPP) $(BLT_INCLUDES_CPP) $(BLT_OPTIMIZE_CPP) $(BLT_DEBUG_CPP) $(BLT_PROFILE_CPP) $(BLT_WARNINGS_CPP) $(BLT_LOCAL_FLAGS) -c $< -o $@

### build .a from .o 
%.a:
	$(BLT_ARCHIVE_A) $@ $(filter %.o %.a %.lib,$^)

### make an executable
BLT_MAKE_EXECUTABLE_COMMAND_C = $(BLT_LINK_C) $(BLT_OPTIMIZE_C) $(BLT_DEBUG_C) $(BLT_PROFILE_C) $(filter %.o %.a %.lib,$^) $(BLT_LIBRARIES_C) -o $@

BLT_MAKE_EXECUTABLE_COMMAND_CPP = $(BLT_LINK_CPP) $(BLT_OPTIMIZE_CPP) $(BLT_DEBUG_CPP) $(BLT_PROFILE_CPP) $(filter %.o %.a %.lib,$^) $(BLT_LIBRARIES_CPP) -o $@

### make an archive
BLT_MAKE_ARCHIVE_COMMAND = $(BLT_ARCHIVE_A) $@  $(filter %.o %.a %.lib,$^)

### clean
.PHONY: clean
clean:
	@rm -f $(BLT_LOCAL_FILES_TO_CLEAN)

### clean-deps
.PHONY: clean-deps
clean-deps: $(BLT_IMPORTED_MODULE_CLEANS) clean

##########################################################################
#    utils
##########################################################################
BLT_COLOR_SET_1   = "[34m"
BLT_COLOR_SET_2   = "[36;1m"
BLT_COLOR_RESET = "[0m"

ifneq ($(BLT_MAKE_OPTIONS),QUIET)
BLT_MAKE_BANNER_START = @echo $(BLT_COLOR_SET_1)================ making\   $(BLT_COLOR_RESET) $(BLT_COLOR_SET_2) $(XXX_CLIENT)::$@ [$(BLT_BUILD_CONFIG)] $(BLT_COLOR_RESET) $(BLT_COLOR_SET_1) =============== $(BLT_COLOR_RESET)

BLT_MAKE_BANNER_END =  @echo $(BLT_COLOR_SET_1)================ done with $(BLT_COLOR_RESET)$(BLT_COLOR_SET_2)$(XXX_CLIENT)::$@ $(BLT_COLOR_RESET) $(BLT_COLOR_SET_1) ================= $(BLT_COLOR_RESET)

BLT_MAKE_BANNER = $(BLT_MAKE_BANNER_START)
endif

BLT_SUB_MAKE = @$(MAKE) $(BLT_MAKE_FLAGS) 
