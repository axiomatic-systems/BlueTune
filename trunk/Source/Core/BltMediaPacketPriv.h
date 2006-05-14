/*****************************************************************
|
|   File: BltMediaPacketPriv.h
|
|   BlueTune - Media Packet Private Interface
|
|   (c) 2002-2003 Gilles Boccon-Gibod
|   Author: Gilles Boccon-Gibod (bok@bok.net)
|
 ****************************************************************/
/** @file
 * BlueTune MediaPacket Interface Header file
 */

#ifndef _BLT_MEDIA_PACKET_PRIV_H_
#define _BLT_MEDIA_PACKET_PRIV_H_

/*----------------------------------------------------------------------
|   includes
+---------------------------------------------------------------------*/
#include "BltMediaPacket.h"

/*----------------------------------------------------------------------
|   prototypes
+---------------------------------------------------------------------*/
BLT_Result BLT_MediaPacket_Create(BLT_Size             size, 
                                  const BLT_MediaType* type,
                                  BLT_MediaPacket**    packet);

#endif /* _BLT_MEDIA_PACKET_PRIV_H_ */
