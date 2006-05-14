/*****************************************************************
|
|   File: BltPcm.h
|
|   BlueTune - PCM Utilities
|
|   (c) 2002-2003 Gilles Boccon-Gibod
|   Author: Gilles Boccon-Gibod (bok@bok.net)
|
 ****************************************************************/
/** @file
 * BlueTune PCM Interface Header file
 */

#ifndef _BLT_PCM_H_
#define _BLT_PCM_H_

/*----------------------------------------------------------------------
|   includes
+---------------------------------------------------------------------*/
#include "Atomix.h"
#include "BltConfig.h"
#include "BltDefs.h"
#include "BltTypes.h"
#include "BltErrors.h"
#include "BltMedia.h"
#include "BltMediaPacket.h"
#include "BltCore.h"

/*----------------------------------------------------------------------
|   types
+---------------------------------------------------------------------*/
typedef struct {
    BLT_MediaType base;
    BLT_UInt32    sample_rate;
    BLT_UInt16    channel_count;
    BLT_UInt8     bits_per_sample;
    BLT_UInt8     sample_format;
} BLT_PcmMediaType;

/*----------------------------------------------------------------------
|   macros
+---------------------------------------------------------------------*/
#define BLT_PCM_MEDIA_TYPE_EXTENSION_CLEAR(_e)  \
do {                                            \
    _e.sample_rate = 0;                         \
    _e.channel_count = 0;                       \
    _e.bits_per_sample = 0;                     \
    _e.sample_format = 0;                       \
} while(0);

/*----------------------------------------------------------------------
|   constants
+---------------------------------------------------------------------*/

/* PCM sample format IDs */
/* _NE means Native Endian */
/* _LE means Little Endian */
/* _BE means Big Endian    */

#define BLT_PCM_SAMPLE_FORMAT_NONE                0

#if 0
#define BLT_PCM_SAMPLE_FORMAT_SIGNED_INT_08       1
#define BLT_PCM_SAMPLE_FORMAT_UNSIGNED_INT_08     2

#define BLT_PCM_SAMPLE_FORMAT_SIGNED_INT_16_BE    3
#define BLT_PCM_SAMPLE_FORMAT_SIGNED_INT_24_BE    4
#define BLT_PCM_SAMPLE_FORMAT_SIGNED_INT_32_BE    5
#define BLT_PCM_SAMPLE_FORMAT_UNSIGNED_INT_16_BE  6
#define BLT_PCM_SAMPLE_FORMAT_UNSIGNED_INT_24_BE  7
#define BLT_PCM_SAMPLE_FORMAT_UNSIGNED_INT_32_BE  8
#define BLT_PCM_SAMPLE_FORMAT_FLOAT_32_BE         9
#define BLT_PCM_SAMPLE_FORMAT_FLOAT_64_BE         10

#define BLT_PCM_SAMPLE_FORMAT_SIGNED_INT_16_LE    11
#define BLT_PCM_SAMPLE_FORMAT_SIGNED_INT_24_LE    12
#define BLT_PCM_SAMPLE_FORMAT_SIGNED_INT_32_LE    13
#define BLT_PCM_SAMPLE_FORMAT_UNSIGNED_INT_16_LE  14
#define BLT_PCM_SAMPLE_FORMAT_UNSIGNED_INT_24_LE  15
#define BLT_PCM_SAMPLE_FORMAT_UNSIGNED_INT_32_LE  16
#define BLT_PCM_SAMPLE_FORMAT_FLOAT_32_LE         17
#define BLT_PCM_SAMPLE_FORMAT_FLOAT_64_LE         18

#if BLT_CONFIG_CPU_BYTE_ORDER == BLT_CPU_BIG_ENDIAN
#define BLT_PCM_SAMPLE_FORMAT_SIGNED_INT_16_NE BLT_PCM_SAMPLE_FORMAT_SIGNED_INT_16_BE
#define BLT_PCM_SAMPLE_FORMAT_SIGNED_INT_24_NE BLT_PCM_SAMPLE_FORMAT_SIGNED_INT_24_BE
#define BLT_PCM_SAMPLE_FORMAT_SIGNED_INT_32_NE BLT_PCM_SAMPLE_FORMAT_SIGNED_INT_32_BE
#define BLT_PCM_SAMPLE_FORMAT_UNSIGNED_INT_16_NE BLT_PCM_SAMPLE_FORMAT_SIGNED_INT_16_BE
#define BLT_PCM_SAMPLE_FORMAT_UNSIGNED_INT_24_NE BLT_PCM_SAMPLE_FORMAT_SIGNED_INT_24_BE
#define BLT_PCM_SAMPLE_FORMAT_UNSIGNED_INT_32_NE BLT_PCM_SAMPLE_FORMAT_SIGNED_INT_32_BE
#define BLT_PCM_SAMPLE_FORMAT_FLOAT_32_NE      BLT_PCM_SAMPLE_FORMAT_FLOAT_32_BE
#define BLT_PCM_SAMPLE_FORMAT_FLOAT_64_NE      BLT_PCM_SAMPLE_FORMAT_FLOAT_64_BE
#else
#define BLT_PCM_SAMPLE_FORMAT_SIGNED_INT_16_NE BLT_PCM_SAMPLE_FORMAT_SIGNED_INT_16_LE
#define BLT_PCM_SAMPLE_FORMAT_SIGNED_INT_24_NE BLT_PCM_SAMPLE_FORMAT_SIGNED_INT_24_LE
#define BLT_PCM_SAMPLE_FORMAT_SIGNED_INT_32_NE BLT_PCM_SAMPLE_FORMAT_SIGNED_INT_32_LE
#define BLT_PCM_SAMPLE_FORMAT_UNSIGNED_INT_16_NE BLT_PCM_SAMPLE_FORMAT_SIGNED_INT_16_LE
#define BLT_PCM_SAMPLE_FORMAT_UNSIGNED_INT_24_NE BLT_PCM_SAMPLE_FORMAT_SIGNED_INT_24_LE
#define BLT_PCM_SAMPLE_FORMAT_UNSIGNED_INT_32_NE BLT_PCM_SAMPLE_FORMAT_SIGNED_INT_32_LE
#define BLT_PCM_SAMPLE_FORMAT_FLOAT_32_NE      BLT_PCM_SAMPLE_FORMAT_FLOAT_32_LE
#define BLT_PCM_SAMPLE_FORMAT_FLOAT_64_NE      BLT_PCM_SAMPLE_FORMAT_FLOAT_64_LE
#endif
#endif

#define BLT_PCM_SAMPLE_FORMAT_SIGNED_INT_BE    1
#define BLT_PCM_SAMPLE_FORMAT_SIGNED_INT_LE    2
#define BLT_PCM_SAMPLE_FORMAT_UNSIGNED_INT_BE  3
#define BLT_PCM_SAMPLE_FORMAT_UNSIGNED_INT_LE  4
#define BLT_PCM_SAMPLE_FORMAT_FLOAT_BE         5
#define BLT_PCM_SAMPLE_FORMAT_FLOAT_LE         6

#if BLT_CONFIG_CPU_BYTE_ORDER == BLT_CPU_BIG_ENDIAN
#define BLT_PCM_SAMPLE_FORMAT_SIGNED_INT_NE    BLT_PCM_SAMPLE_FORMAT_SIGNED_INT_BE
#define BLT_PCM_SAMPLE_FORMAT_UNSIGNED_INT_NE  BLT_PCM_SAMPLE_FORMAT_UNSIGNED_INT_BE
#define BLT_PCM_SAMPLE_FORMAT_FLOAT_NE         BLT_PCM_SAMPLE_FORMAT_FLOAT_BE
#else
#define BLT_PCM_SAMPLE_FORMAT_SIGNED_INT_NE    BLT_PCM_SAMPLE_FORMAT_SIGNED_INT_LE
#define BLT_PCM_SAMPLE_FORMAT_UNSIGNED_INT_NE  BLT_PCM_SAMPLE_FORMAT_UNSIGNED_INT_LE
#define BLT_PCM_SAMPLE_FORMAT_FLOAT_NE         BLT_PCM_SAMPLE_FORMAT_FLOAT_LE
#endif

/*----------------------------------------------------------------------
|   globals
+---------------------------------------------------------------------*/
extern const BLT_MediaType BLT_GenericPcmMediaType;

/*----------------------------------------------------------------------
|   prototypes
+---------------------------------------------------------------------*/
extern void
BLT_PcmMediaType_Init(BLT_PcmMediaType* media_type);

extern BLT_Result
BLT_Pcm_CanConvert(const BLT_MediaType* from, const BLT_MediaType* to);

extern BLT_Result
BLT_Pcm_ConvertMediaPacket(BLT_Core*         core,
                           BLT_MediaPacket*  in_packet, 
                           BLT_PcmMediaType* out_type, 
                           BLT_MediaPacket** out_packet);


#endif /* _BLT_PCM_H_ */
