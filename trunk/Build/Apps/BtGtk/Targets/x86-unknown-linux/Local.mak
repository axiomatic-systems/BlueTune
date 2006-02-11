#######################################################################
#
#    Local Target Makefile
#
#    (c) 2002-2003 Gilles Boccon-Gibod
#    Author: Gilles Boccon-Gibod (bok@bok.net)
#
#######################################################################

#######################################################################
#    local settings
#######################################################################

## pick one of those
GTK_PACKAGE = gtk+
#GTK_PACKAGE = gtk+-2.0

GTK_INCLUDES := $(shell pkg-config --cflags $(GTK_PACKAGE))
GTK_LIBS     := $(shell pkg-config --libs $(GTK_PACKAGE))

BtGtk.o: BLT_INCLUDES_CPP += $(GTK_INCLUDES)
BtGtk.d: BLT_INCLUDES_CPP += $(GTK_INCLUDES)
BtGtk.o: BLT_WARNINGS_CPP = -Wall -Wno-long-long

gbluetune: BLT_LIBRARIES_CPP += $(GTK_LIBS)

