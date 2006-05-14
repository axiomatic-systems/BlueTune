/*****************************************************************
|
|   File: BltDebug.h
|
|   BlueTune - Debug Support
|
|   (c) 2002-2003 Gilles Boccon-Gibod
|   Author: Gilles Boccon-Gibod (bok@bok.net)
|
 ****************************************************************/
/** @file
 * Header file for debug support
 */

#ifndef _BLT_DEBUG_H_
#define _BLT_DEBUG_H_

/*----------------------------------------------------------------------
|   includes
+---------------------------------------------------------------------*/
#include "Atomix.h"

/*----------------------------------------------------------------------
|   import atomix functions
+---------------------------------------------------------------------*/
#define BLT_Debug ATX_Debug

/*----------------------------------------------------------------------
|   logging
+---------------------------------------------------------------------*/
typedef enum {
    BLT_LOG_CHANNEL_CORE = 0,
    BLT_LOG_CHANNEL_STREAM,
    BLT_LOG_CHANNEL_PLUGINS,
    BLT_LOG_CHANNEL_OTHER
} BLT_LogChannel;

#if defined(BLT_DEBUG)
extern int BLT_LogLevels[BLT_LOG_CHANNEL_OTHER+1];
#define BLT_LOG(c, l, m)                                                \
do {                                                                    \
    unsigned char _blt_lgch = (unsigned char)(l);                       \
    if (_blt_lgch < sizeof(BLT_LogLevels)/sizeof(BLT_LogLevels[0])) {   \
        if (BLT_LogLevels[_blt_lgch] > (int)(l)) {                      \
            BLT_Debug m ;                                               \
        }                                                               \
      }                                                                 \
} while (0);
       
#else
#define BLT_LOG(c, l, m)
#endif /* BLT_DEBUG */

#endif /* _BLT_DEBUG_H_ */
