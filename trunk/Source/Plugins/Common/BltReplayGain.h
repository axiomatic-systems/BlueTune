/*****************************************************************
|
|      File: BltReplayGain.h
|
|      ReplayGain common definitions
|
|      (c) 2002-2006 Gilles Boccon-Gibod
|      Author: Gilles Boccon-Gibod (bok@bok.net)
|
 ****************************************************************/

#ifndef _BLT_REPLAY_GAIN_H_
#define _BLT_REPLAY_GAIN_H_

/*----------------------------------------------------------------------
|       includes
+---------------------------------------------------------------------*/
#include "BltTypes.h"
#include "BltModule.h"
#include "BltStream.h"

/*----------------------------------------------------------------------
|       constants
+---------------------------------------------------------------------*/
#define BLT_REPLAY_GAIN_PROPERTY_TRACK_GAIN "ReplayGain/TrackGain"
#define BLT_REPLAY_GAIN_PROPERTY_ALBUM_GAIN "ReplayGain/AlbumGain"

#define BLT_VORBIS_COMMENT_REPLAY_GAIN_TRACK_GAIN "REPLAYGAIN_TRACK_GAIN"
#define BLT_VORBIS_COMMENT_REPLAY_GAIN_ALBUM_GAIN "REPLAYGAIN_ALBUM_GAIN"

/*----------------------------------------------------------------------
|       functions
+---------------------------------------------------------------------*/
extern BLT_Result
BLT_ReplayGain_SetStreamProperties(BLT_Stream* stream,
                                   float       track_gain,
                                   BLT_Boolean track_gain_set,
                                   float       album_gain,
                                   BLT_Boolean album_gain_set);

#endif /* _BLT_REPLAY_GAIN_H_ */
