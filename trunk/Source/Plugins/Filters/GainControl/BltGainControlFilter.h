/*****************************************************************
|
|      Gain Control Filter Module
|
|      (c) 2002-2006 Gilles Boccon-Gibod
|      Author: Gilles Boccon-Gibod (bok@bok.net)
|
 ****************************************************************/

#ifndef _BLT_GAIN_CONTROL_FILTER_H_
#define _BLT_GAIN_CONTROL_FILTER_H_

/*----------------------------------------------------------------------
|       includes
+---------------------------------------------------------------------*/
#include "BltTypes.h"
#include "BltModule.h"

/*----------------------------------------------------------------------
|       constants
+---------------------------------------------------------------------*/
#define BLT_GAIN_CONTROL_FILTER_OPTION_DO_REPLAY_GAIN "Plugins/GainControlFilter/DoReplayGain"

#define BLT_REPLAY_GAIN_TRACK_GAIN_VALUE  "ReplayGain/TrackGain"
#define BLT_REPLAY_GAIN_TRACK_PEAK_VALUE  "ReplayGain/TrackPeak"
#define BLT_REPLAY_GAIN_ALBUM_GAIN_VALUE  "ReplayGain/AlbumGain"
#define BLT_REPLAY_GAIN_ALBUM_PEAK_VALUE  "ReplayGain/AlbumPeak"

/*----------------------------------------------------------------------
|       module
+---------------------------------------------------------------------*/
BLT_Result BLT_GainControlFilterModule_GetModuleObject(BLT_Module** module);

#endif /* _BLT_GAIN_CONTROL_FILTER_H_ */
