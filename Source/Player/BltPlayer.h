/*****************************************************************
|
|   BlueTune - Async Layer
|
|   (c) 2002-2006 Gilles Boccon-Gibod
|   Author: Gilles Boccon-Gibod (bok@bok.net)
|
 ****************************************************************/
/** @file
 * BLT_Player API
 */

#ifndef _BLT_PLAYER_H_
#define _BLT_PLAYER_H_

/** @defgroup BLT_Player BLT_Player Class
 * @{
 */

/*----------------------------------------------------------------------
|   includes
+---------------------------------------------------------------------*/
#include "Neptune.h"
#include "BltDecoder.h"
#include "BltDecoderClient.h"
#include "BltDecoderServer.h"

/*----------------------------------------------------------------------
|   BLT_Player
+---------------------------------------------------------------------*/
/**
 * The BLT_Player class is the asynchronous client API.
 * A BLT_Player player creates internally a thread in which a decoder 
 * (BLT_Decoder) runs in a decoding loop. The decoding loop receives
 * commands that are sent to it when calling some of the methods of this
 * class. Those commands perform basic player controls, such as the ability
 * to select the input, the output, to play, stop, pause, seek, etc...
 */
class BLT_Player : public BLT_DecoderClient
{
 public:
    /**
     * Construct a new instance.
     * @param queue pointer to a message queue that should receive messages
     * from the decoder thread. If this parameter is NULL, a message queue
     * will be created automatically.
     * WARNING: it is important that this queue not be destroyed before this
     * player object is destroyed, because it may receive notification 
     * messages. If you need to destroy the queue in the destructor of a 
     * subclass of this class, call the Shutdown() method before destroying
     * the message queue to ensure that no more notification messages will 
     * be posted to the queue.
     */
    BLT_Player(NPT_MessageQueue* queue = NULL);

    /**
     * Destruct an instance.
     */
    virtual ~BLT_Player();

    /**
     * Dequeue and dispatch one message from the queue. 
     * @param blocking Indicate whether this call should block until a
     * message is available, or if it should return right away, regardless
     * of whether a message is available or not.
     */
    virtual BLT_Result PumpMessage(bool blocking = true);

    /**
     * Set the input of the decoder.
     * @param name Name of the input
     * @param type Mime-type of the input, if known, or NULL
     */
    virtual BLT_Result SetInput(BLT_CString name, BLT_CString type = NULL);

    /**
     * Set the output of the decoder.
     * @param name Name of the output
     * @param type Mime-type of the output, if known
     */
    virtual BLT_Result SetOutput(BLT_CString name, BLT_CString type = NULL);

    /**
     * Instruct the decoder to start decoding media packets and send them
     * to the output.
     */
    virtual BLT_Result Play();

    /**
     * Instruct the decoder to stop decoding and become idle.
     */
    virtual BLT_Result Stop();

    /**
     * Instruct the decoder to pause decoding and become idle.
     * The difference between pausing and stopping is that pausing can
     * be resumed without any gaps, whereas stopping may release internal
     * resources and flush decoding buffers, so a Play() after a Stop()
     * may result in an audible gap.
     */
    virtual BLT_Result Pause();

    /**
     * Instruct the decoder to seek to a specifc time value.
     * @param time the time to seek to, expressed in milliseconds.
     */
    virtual BLT_Result SeekToTime(BLT_UInt32 time);

    /**
     * Instruct the decoder to seek to a specific time stamp.
     * @param h Hours
     * @param m Minutes
     * @param s Seconds
     * @param f Fractions
     */
    virtual BLT_Result SeekToTimeStamp(BLT_UInt8 h, 
                                       BLT_UInt8 m, 
                                       BLT_UInt8 s, 
                                       BLT_UInt8 f);

    /**
     * Instruct the decoder to seek to a specific position.
     * @param offset Offset between 0 and range
     * @param range Maximum value of offset. The range is an arbitrary
     * scale. For example, if offset=1 and range=2, this means that
     * the decoder should seek to exacly the middle point of the input.
     * Or if offset=25 and range=100, this means that the decoder should
     * seek to the point that is at 25/100 of the total input.
     */
    virtual BLT_Result SeekToPosition(BLT_Size offset, BLT_Size range);

    /**
     * Register a module with the decoder.
     * @param module Pointer to a module object.
     */
    virtual BLT_Result RegisterModule(BLT_Module* module);

    /**
     * Instruct the decoder to instantiate a new node and add it to its
     * decoding stream.
     * @param name Name of the node to add.
     */
    virtual BLT_Result AddNode(BLT_CString name);

    /**
     * Shutdown the player. 
     * Use this method before deleting the player if you need to ensure that
     * no more asynchronous event callbacks will be made. 
     * When this method returns, no other method can be called appart from
     * the destructor.
     */
    virtual BLT_Result Shutdown();

    /**
     * Send this player a NPT_TerminateMessage message that will cause any caller
     * waiting for a message on the incoming message queue to be
     * unblocked.
     * CAUTION: this is an advanced function. Only call this is you know 
     * exactly what you are doing (it is not often needed).
     */
    virtual BLT_Result Interrupt();

private:
    /**
     * Instance of a decoding thread that implements the BLT_DecoderServer interface.
     * All player commands are delegated to this instance.
     */
    BLT_DecoderServer* m_Server;
};

/** @} */

#endif /* _BLT_PLAYER_H_ */
