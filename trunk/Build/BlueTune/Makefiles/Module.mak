#######################################################################
#
#    Module Makefile
#
#    (c) 2002-2003 Gilles Boccon-Gibod
#    Author: Gilles Boccon-Gibod (bok@bok.net)
#
#######################################################################

#######################################################################
# module dependencies
#######################################################################
BLT_IMPORT_MODULES = Player
include $(BLT_BUILD_INCLUDES)/ImportModuleDeps.mak

#######################################################################
# clean
#######################################################################
BLT_LOCAL_FILES_TO_CLEAN = *.d *.o *.a

#######################################################################
# targets
#######################################################################
libBlueTune.a: $(BLT_IMPORTED_MODULE_LIBS) $(BLT_MODULE_OBJECTS) $(BLT_MODULE_LIBRARIES)
	$(BLT_MAKE_BANNER_START)
	$(BLT_MAKE_ARCHIVE_COMMAND)
	$(BLT_MAKE_BANNER_END)


#######################################################################
# targets
#######################################################################
BLT_SDK_DIR         = SDK
BLT_SDK_LIB_DIR     = SDK/lib
BLT_SDK_INCLUDE_DIR = SDK/include

.PHONY: dirs
dirs:
	-@mkdir -p $(BLT_SDK_DIR)
	-@mkdir -p $(BLT_SDK_LIB_DIR)
	-@mkdir -p $(BLT_SDK_INCLUDE_DIR)

.PHONY: SDK
SDK: dirs libBlueTune.a
	$(BLT_MAKE_BANNER_START)
	@cp libBlueTune.a $(BLT_SDK_LIB_DIR)
	@cp $(BLT_SOURCE_ROOT)/Config/*.h $(BLT_SDK_INCLUDE_DIR)
	@cp $(BLT_SOURCE_ROOT)/Core/*.h $(BLT_SDK_INCLUDE_DIR)
	@cp $(BLT_SOURCE_ROOT)/Decoder/*.h $(BLT_SDK_INCLUDE_DIR)
	@cp $(BLT_SOURCE_ROOT)/Player/*.h $(BLT_SDK_INCLUDE_DIR)
	@cp $(BLT_SOURCE_ROOT)/Plugins/*/*/*.h $(BLT_SDK_INCLUDE_DIR)
	@cp $(BLT_SOURCE_ROOT)/BlueTune/BlueTune.h $(BLT_SDK_INCLUDE_DIR)
	$(BLT_MAKE_BANNER_END)
