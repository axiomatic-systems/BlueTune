/*****************************************************************
|
|      File: BltReplayGain.c
|
|      ReplayGain common functions
|
|      (c) 2002-2006 Gilles Boccon-Gibod
|      Author: Gilles Boccon-Gibod (bok@bok.net)
|
****************************************************************/

/*----------------------------------------------------------------------
|       includes
+---------------------------------------------------------------------*/
#include "BltReplayGain.h"

/*----------------------------------------------------------------------
|       BLT_ReplayGain_SetStreamProperties
+---------------------------------------------------------------------*/
BLT_Result
BLT_ReplayGain_SetStreamProperties(BLT_Stream* stream,
                                   float       track_gain,
                                   BLT_Boolean track_gain_set,
                                   float       album_gain,
                                   BLT_Boolean album_gain_set)
{
    /* quick check */
    if (ATX_OBJECT_IS_NULL(stream)) return BLT_FAILURE;

    /* set the stream properties */
    if (track_gain_set || album_gain_set) {
        ATX_Properties properties;

        /* get a reference to the stream properties */
        if (BLT_SUCCEEDED(BLT_Stream_GetProperties(stream, &properties))) {
            ATX_PropertyValue property_value;
            if (track_gain_set) {
                property_value.integer = (ATX_Int32)(track_gain*100.0f);
                ATX_Properties_SetProperty(&properties,
                    BLT_REPLAY_GAIN_PROPERTY_TRACK_GAIN,
                    ATX_PROPERTY_TYPE_INTEGER,
                    &property_value);
            }
            if (album_gain_set) {
                property_value.integer = (ATX_Int32)(album_gain*100.0f);
                ATX_Properties_SetProperty(&properties,
                    BLT_REPLAY_GAIN_PROPERTY_ALBUM_GAIN,
                    ATX_PROPERTY_TYPE_INTEGER,
                    &property_value);
            }
        }
    }

    return BLT_SUCCESS;
}
