#######################################################################
#
#    Local Taget Makefile
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
BltFlacDecoder.o: BLT_FLAGS_C = -ansi -fno-strict-aliasing