#######################################################################
#
#    Local Target Makefile
#
#    (c) 2002-2003 Gilles Boccon-Gibod
#    Author: Gilles Boccon-Gibod (bok@bok.net)
#
#######################################################################

#######################################################################
#    target configuration
#######################################################################
BLT_PLUGINS_CDDA_TYPE = Linux

#######################################################################
#    local target changes
#######################################################################
BltNeutrinoOutput.d: BLT_DEFINES_C += -D_XOPEN_SOURCE
BltNeutrinoOutput.o: BLT_DEFINES_C += -D_XOPEN_SOURCE
BltFlacDecoder.o: BLT_FLAGS_C = -ansi
