/*****************************************************************
|
|      BlueTune - Top Level Header
|
|      (c) 2002-2007 Gilles Boccon-Gibod
|      Author: Gilles Boccon-Gibod (bok@bok.net)
|
 ****************************************************************/
/** @file
 * Master Top Level Header file included by BlueTune clients
 */

/**
 @mainpage BlueTune SDK

 @section intro Introduction

 @section getting_started Getting Started

 */

#ifndef _BLUETUNE_H_
#define _BLUETUNE_H_

/*----------------------------------------------------------------------
|       includes
+---------------------------------------------------------------------*/
#include "BltConfig.h"
#include "BltDefs.h"
#include "BltTypes.h"
#include "BltErrors.h"
#include "BltModule.h"
#include "BltRegistry.h"
#include "BltCore.h"
#include "BltStream.h"
#include "BltTime.h"
#include "BltMedia.h"
#include "BltMediaNode.h"
#include "BltMediaPort.h"
#include "BltMediaPacket.h"
#include "BltBuiltins.h"
#include "BltPacketProducer.h"
#include "BltPacketConsumer.h"
#include "BltByteStreamUser.h"
#include "BltByteStreamProvider.h"
#include "BltDecoder.h"
#include "BltEvent.h"
#include "BltEventListener.h"

#ifdef __cplusplus
#include "BltPlayer.h"
#endif /* __cplusplus */

#endif /* _BLUETUNE_H_ */
