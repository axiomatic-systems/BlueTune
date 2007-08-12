/*****************************************************************
|
|   BlueTune - Sync Layer
|
|   (c) 2002-2006 Gilles Boccon-Gibod
|   Author: Gilles Boccon-Gibod (bok@bok.net)
|
 ****************************************************************/
/** @file
 * BLT_Decoder API
 */

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

/** @defgroup BLT_Decoder BLT_Decoder Class
 * @{
 */

/*----------------------------------------------------------------------
|   constants
+---------------------------------------------------------------------*/
#define BLT_DECODER_DEFAULT_OUTPUT_NAME "!default"

/*----------------------------------------------------------------------
|   types
+---------------------------------------------------------------------*/
/**
 * BLT_Decoder object.
 * This is the synchronous client API.
 * A decoder creates a core engine, and manages a chain of media nodes
 * including the input and output nodes. It acts as a media data 'pump'
 * getting data from the input, processing it through all the media nodes
 * in the chain, until it reaches the output. The decoder also encapsulates
 * the core functions, such as the registration of plugin modules, etc...
 */
typedef struct BLT_Decoder BLT_Decoder;

/**
 * Represents the current status of a BLT_Decoder object.
 */
typedef struct {
    BLT_StreamInfo     stream_info; /**< Stream info       */
    BLT_StreamPosition position;    /**< Stream position   */
    BLT_TimeStamp      time_stamp;  /**< Timestamp         */
} BLT_DecoderStatus;

/**
 * Property scopes represent the scope of a property. The scope indicates
 * to what part of the system the property applies.
 */
typedef enum {
    BLT_PROPERTY_SCOPE_CORE,
    BLT_PROPERTY_SCOPE_STREAM,
    BLT_PROPERTY_SCOPE_MODULE
} BLT_PropertyScope;

/*----------------------------------------------------------------------
|   prototypes
+---------------------------------------------------------------------*/
#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/**
 * Create a BLT_Decoder object.
 */
BLT_Result BLT_Decoder_Create(BLT_Decoder** decoder);

/**
 * Destroy a BLT_Decoder object.
 */
BLT_Result BLT_Decoder_Destroy(BLT_Decoder* decoder);

/**
 * Register all the builtin plugins modules with a BLT_Decoder object.
 */
BLT_Result BLT_Decoder_RegisterBuiltins(BLT_Decoder* decoder);

/**
 * Register a specific plugin module with a BLT_Decoder object.
 * @param module Pointer to a module object to register.
 */
BLT_Result BLT_Decoder_RegisterModule(BLT_Decoder* decoder,
                                      BLT_Module*  module);

/**
 * Set a BLT_Decoder object's input.
 * @param name Name of the input.
 * @param type Mime-type of the input, if known, or NULL.
 */
BLT_Result BLT_Decoder_SetInput(BLT_Decoder*  decoder, 
                                BLT_CString   name, 
                                BLT_CString   type);

/**
 * Set a BLT_Decoder object's output.
 * @param name Name of the output.
 * @param type Mime-type of the output, if known, or NULL.
 */
BLT_Result BLT_Decoder_SetOutput(BLT_Decoder* decoder, 
                                 BLT_CString  name, 
                                 BLT_CString  type);

/**
 * Add a node to a BLT_Decoder object's stream node graph.
 * @param where Pointer to the node before which the new node will
 * be added. If this parameter is NULL, the node will be added before
 * the output node.
 * @param name Name of the node to instantiate and add.
 */
BLT_Result BLT_Decoder_AddNodeByName(BLT_Decoder*   decoder, 
                                     BLT_MediaNode* where,
                                     BLT_CString    name);

/**
 * Get the ATX_Properties object representing the properties of a
 * BLT_Decoder object.
 */
BLT_Result BLT_Decoder_GetProperties(BLT_Decoder*     decoder,
                                     ATX_Properties** properties);

/**
 * Get the current status of a BLT_Decoder object.
 * @param status Pointer to a BLT_DecoderStatus structure where the
 * status will be returned.
 */
BLT_Result BLT_Decoder_GetStatus(BLT_Decoder*       decoder,
                                 BLT_DecoderStatus* status);

/**
 * Get the ATX_Properties object representing the properties of a
 * BLT_Decoder object's stream.
 * @param properties Pointer to a pointer where a pointer to an 
 * APX_Properties object will be returned. The caller can then call
 * methods of the ATX_Properties object to query stream properties.
 */
BLT_Result BLT_Decoder_GetStreamProperties(BLT_Decoder*     decoder,
                                           ATX_Properties** properties);

/**
 * Process on media packet through a BLT_Decoder object's stream.
 */
BLT_Result BLT_Decoder_PumpPacket(BLT_Decoder* decoder);

/**
 * Tell a BLT_Decoder object that decoding is stopped. This allows the
 * media nodes in the decoder's graph to release some resources if they
 * need to. There is no special function to call when decoding resumes,
 * as a call to BLT_Decoder_PumpPacket will automatically signal all the
 * media nodes to restart if necessary.
 */
BLT_Result BLT_Decoder_Stop(BLT_Decoder* decoder);

/**
 * Tell a BLT_Decoder object that decoding is paused. This allows the
 * media nodes in the decoder's graph to release some resources if they
 * need to. There is no special function to call when decoding resumes,
 * as a call to BLT_Decoder_PumpPacket will automatically signal all the
 * media nodes to restart if necessary.
 * The difference between pausing and stopping is that pausing can
 * be resumed without any gaps, whereas stopping may release internal
 * resources and flush decoding buffers, so continuing decoder after a 
 * stop may result in an audible gap.
 */
BLT_Result BLT_Decoder_Pause(BLT_Decoder* decoder);

/**
 * Seek to a specific time.
 * @param Time to which to seek, in milliseconds.
 */
BLT_Result BLT_Decoder_SeekToTime(BLT_Decoder* decoder, BLT_UInt32 time);

/** Seek to a specific position.
 * @param offset Offset between 0 and range
 * @param range Maximum value of offset. The range is an arbitrary
 * scale. For example, if offset=1 and range=2, this means that
 * the decoder should seek to exacly the middle point of the input.
 * Or if offset=25 and range=100, this means that the decoder should
 * seek to the point that is at 25/100 of the total input.
 */
BLT_Result BLT_Decoder_SeekToPosition(BLT_Decoder* decoder,
                                      BLT_Size     offset,
                                      BLT_Size     range);

/**
 * Set a BLT_Decoder object's event listener. The listener object's
 * notification functions will be called when certain events occur.
 * @param listener Pointer to the BLT_EventListener object that will
 * be notified of events.
 */
BLT_Result BLT_Decoder_SetEventListener(BLT_Decoder*       decoder,
                                        BLT_EventListener* listener);
                                               
#ifdef __cplusplus
}
#endif /* __cplusplus */

/** @} */

#endif /* _BLT_DECODER_H_ */
