/*****************************************************************
|
|      BlueTune - Top Level Header
|
|      (c) 2002-2007 Gilles Boccon-Gibod
|      Author: Gilles Boccon-Gibod (bok@bok.net)
|
 ****************************************************************/
/** @file
 * Master Header file included by BlueTune client applications.
 *
 * Client Applications should only need to include this file, as it 
 * includes all the more specific include files required to use the API
 */

/** 
@mainpage BlueTune SDK

@section intro Introduction
 
The BlueTune SDK contains all the software components necessary to 
build and use the BlueTune Media Player Framework. This includes
the BlueTune code framework and plugins, the Neptune C++ runtime
library, the Atomix C runtime library, as well as other modules.

@section getting_started Getting Started

There are two programming interfaces in the BlueTune SDK. The low-level
synchronous API, also called the Decoder API, and the high-level 
asynchronous API, also called the Player API.

@section low_level Low Level API
 
The low-level API provides a set of functions to do synchronous 
decoding/playback of media. With this API, the caller creates a BLT_Decoder
object, sets the input, output, and may register a number of plugin
components implementing Media Nodes. Then it can decode and output media 
packets one by one, until the end of the input has been reached.

@section high_level High Level API
 
The high-level API is an interface to an asynchrous decoder built
on top of the low-level synchronous API. The decoder runs in its own
thread. The client application communicates with the decoder thread 
through a message queue.

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
