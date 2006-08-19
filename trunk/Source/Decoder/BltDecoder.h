/*****************************************************************
|
|   BlueTune - Sync Layer
|
|   (c) 2002-2006 Gilles Boccon-Gibod
|   Author: Gilles Boccon-Gibod (bok@bok.net)
|
 ****************************************************************/

#ifndef _BLT_DECODER_H_
#define _BLT_DECODER_H_

/*----------------------------------------------------------------------
|   includes
+---------------------------------------------------------------------*/
#include "BltDefs.h"
#include "BltTypes.h"
#include "BltStream.h"
#include "BltTime.h"
#include "BltEventListener.h"

/*----------------------------------------------------------------------
|   constants
+---------------------------------------------------------------------*/
#define BLT_DECODER_DEFAULT_OUTPUT_NAME "!default"

/*----------------------------------------------------------------------
|   types
+---------------------------------------------------------------------*/
typedef struct BLT_Decoder BLT_Decoder;
typedef struct {
    BLT_StreamInfo     stream_info;
    BLT_StreamPosition position;
    BLT_TimeStamp      time_stamp;
} BLT_DecoderStatus;

/*----------------------------------------------------------------------
|   prototypes
+---------------------------------------------------------------------*/
#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

extern BLT_Result BLT_Decoder_Create(BLT_Decoder** decoder);
extern BLT_Result BLT_Decoder_Destroy(BLT_Decoder* decoder);
extern BLT_Result BLT_Decoder_RegisterBuiltins(BLT_Decoder* decoder);
extern BLT_Result BLT_Decoder_RegisterModule(BLT_Decoder* decoder,
                                             BLT_Module*  module);
extern BLT_Result BLT_Decoder_LoadModule(BLT_Decoder* decoder,
                                         BLT_CString  name);
extern BLT_Result BLT_Decoder_SetInput(BLT_Decoder*  decoder, 
                                       BLT_CString   name, 
                                       BLT_CString   type);
extern BLT_Result BLT_Decoder_SetOutput(BLT_Decoder* decoder, 
                                        BLT_CString  name, 
                                        BLT_CString  type);
extern BLT_Result BLT_Decoder_AddNodeByName(BLT_Decoder*   decoder, 
                                            BLT_MediaNode* where,
                                            BLT_CString    name);
extern BLT_Result BLT_Decoder_GetSettings(BLT_Decoder*     decoder,
                                          ATX_Properties** settings);
extern BLT_Result BLT_Decoder_GetStatus(BLT_Decoder*       decoder,
                                        BLT_DecoderStatus* status);
extern BLT_Result BLT_Decoder_GetStreamProperties(BLT_Decoder*     decoder,
                                                  ATX_Properties** properties);
extern BLT_Result BLT_Decoder_PumpPacket(BLT_Decoder* decoder);
extern BLT_Result BLT_Decoder_Stop(BLT_Decoder* decoder);
extern BLT_Result BLT_Decoder_Pause(BLT_Decoder* decoder);
extern BLT_Result BLT_Decoder_SeekToTime(BLT_Decoder* decoder,
                                         BLT_Cardinal time);
extern BLT_Result BLT_Decoder_SeekToPosition(BLT_Decoder* decoder,
                                             BLT_Size     offset,
                                             BLT_Size     range);
extern BLT_Result BLT_Decoder_SetEventListener(BLT_Decoder*       decoder,
                                               BLT_EventListener* listener);
                                               
#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* _BLT_DECODER_H_ */
