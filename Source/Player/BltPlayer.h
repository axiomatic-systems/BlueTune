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
 * 
 * An application can choose to either subclass this class and override
 * one or more of the virtual notification callbacks methods (the OnXXX
 * methods of the BLT_DecoderClient_MessageHandler interface class from which 
 * this class inherits), or create an instance of this class and pass an event 
 * listener to the SetEventListener() method to receive notification callbacks.
 */
class BLT_Player : public BLT_DecoderClient
{
 public:
    /**
     * A class must derive from this interface class if it wants
     * to receive event notifications from the player.
     */
    typedef BLT_DecoderClient_MessageHandler EventListener;
     
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
     * Set the event listener that will be called when notification
     * messages are received. By default, there is no event listener
     * when a player is created, so notification messages are just
     * ignored, unless a subclass of this class overrides the virtual
     * methods that handle notification messages (OnXXX virtual methods
     * of the BLT_DecoderClient_MessageHandler class).
     */
    void SetEventListener(EventListener* listener);
    
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
     * @param type Mime-type of the output, if known.
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
     * Ping the decoder.
     * When the decoding thread processes the ping message, it
     * sends back a pong reply, which will invoke the OnPongNotification
     * notification method when received.
     * @param cookie Arbitrary pointer value that will be passed back
     * along with the pong notification message. This is typically used
     * by the caller to keep any state information associated with this
     * call.
     */
    virtual BLT_Result Ping(const void* cookie);

    /**
     * Register a module with the decoder.
     * NOTE: the module's methods will be called in the context of
     * the decoding thread. The caller of this method should not
     * make any call to the module's methods after this method 
     * call returns.
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
     * Set a property.
     * @param scope Scope enum specifying what type of property is being set.
     * @param target Name of the target to which this property applies. For
     * Core (BLT_PROPERTY_SCOPE_CORE) and Stream (BLT_PROPERTY_SCOPE_STREAM)
     * scopes this parameter must be NULL.
     * @param name Name of the property to set.
     * @param value Pointer to the property's value. If this parameter is
     * NULL, the property will be deleted.
     */
    virtual BLT_Result SetProperty(BLT_PropertyScope        scope,
                                   const char*              target,
                                   const char*              name,
                                   const ATX_PropertyValue* value);

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

protected:
    // BLT_DecoderClient_MessageHandler methods
    virtual void OnAckNotification(BLT_DecoderServer_Message::CommandId id) {
        if (m_Listener) m_Listener->OnAckNotification(id);
    }
    virtual void OnNackNotification(BLT_DecoderServer_Message::CommandId id,
                                    BLT_Result                           result) {
        if (m_Listener) m_Listener->OnNackNotification(id, result);
    }
    virtual void OnPongNotification(const void* cookie) {
        if (m_Listener) m_Listener->OnPongNotification(cookie);
    }
    virtual void OnDecoderStateNotification(BLT_DecoderServer::State state) {
        if (m_Listener) m_Listener->OnDecoderStateNotification(state);
    }
    virtual void OnStreamTimeCodeNotification(BLT_TimeCode timecode) {
        if (m_Listener) m_Listener->OnStreamTimeCodeNotification(timecode);
    }
    virtual void OnStreamPositionNotification(BLT_StreamPosition& position) {
        if (m_Listener) m_Listener->OnStreamPositionNotification(position);
    }
    virtual void OnStreamInfoNotification(BLT_Mask        update_mask, 
                                          BLT_StreamInfo& info) {
        if (m_Listener) m_Listener->OnStreamInfoNotification(update_mask, info);
    }
    virtual void OnPropertyNotification(BLT_PropertyScope        scope,
                                        const char*              source,
                                        const char*              name,
                                        const ATX_PropertyValue* value) {
        if (m_Listener) m_Listener->OnPropertyNotification(scope, source, name, value);
    }

private:
    /**
     * Instance of a decoding thread that implements the BLT_DecoderServer interface.
     * All player commands are delegated to this instance.
     */
    BLT_DecoderServer* m_Server;
    
    /**
     * Object that will handle notification callbacks, if not NULL.
     */
    BLT_DecoderClient_MessageHandler* m_Listener;
};

/** @} */

#endif /* _BLT_PLAYER_H_ */
