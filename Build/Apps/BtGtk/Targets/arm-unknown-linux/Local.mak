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
BtGtk.o: BLT_INCLUDES_CPP += -I/usr/include/gtk-1.2 -I/usr/include/glib-1.2 -I/usr/lib/glib/include 
BtGtk.o: BLT_WARNINGS_CPP = -Wall -Wno-long-long
gbluetune: BLT_LIBRARIES_CPP += -L/usr/lib -L/usr/X11R6/lib -lgtk -lgdk -rdynamic -lgmodule -lglib -ldl -lXi -lXext -lX11 -lm
