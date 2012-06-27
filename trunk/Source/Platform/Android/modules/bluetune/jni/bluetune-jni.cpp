/*****************************************************************
|
|      Android JNI Interface
|
|      (c) 2002-2012 Gilles Boccon-Gibod
|      Author: Gilles Boccon-Gibod (bok@bok.net)
|
 ****************************************************************/

/*----------------------------------------------------------------------
|       includes
+---------------------------------------------------------------------*/
#include <assert.h>
#include <jni.h>
#include <string.h>
#include <sys/types.h>

#include "bluetune-jni.h"
#include "BlueTune.h"

#include <android/log.h>

/*----------------------------------------------------------------------
|   logging
+---------------------------------------------------------------------*/
ATX_SET_LOCAL_LOGGER("bluetune.android.jni")
NPT_SET_LOCAL_LOGGER("bluetune.android.jni")

/*----------------------------------------------------------------------
|   functions
+---------------------------------------------------------------------*/
__attribute__((constructor)) static void onDlOpen(void)
{
}

static jint MapCommandId(BLT_DecoderServer_Message::CommandId id)
{
    switch (id) {
        case BLT_DecoderServer_Message::COMMAND_ID_SET_INPUT:        return com_bluetune_player_Player_COMMAND_ID_SET_INPUT;
        case BLT_DecoderServer_Message::COMMAND_ID_SET_OUTPUT:       return com_bluetune_player_Player_COMMAND_ID_SET_OUTPUT;
        case BLT_DecoderServer_Message::COMMAND_ID_SET_VOLUME:       return com_bluetune_player_Player_COMMAND_ID_SET_VOLUME;
        case BLT_DecoderServer_Message::COMMAND_ID_PLAY:             return com_bluetune_player_Player_COMMAND_ID_PLAY;
        case BLT_DecoderServer_Message::COMMAND_ID_STOP:             return com_bluetune_player_Player_COMMAND_ID_STOP;
        case BLT_DecoderServer_Message::COMMAND_ID_PAUSE:            return com_bluetune_player_Player_COMMAND_ID_PAUSE;
        case BLT_DecoderServer_Message::COMMAND_ID_PING:             return com_bluetune_player_Player_COMMAND_ID_PING;
        case BLT_DecoderServer_Message::COMMAND_ID_SEEK_TO_TIME:     return com_bluetune_player_Player_COMMAND_ID_SEEK_TO_TIME;
        case BLT_DecoderServer_Message::COMMAND_ID_SEEK_TO_POSITION: return com_bluetune_player_Player_COMMAND_ID_SEEK_TO_POSITION;
        case BLT_DecoderServer_Message::COMMAND_ID_REGISTER_MODULE:  return com_bluetune_player_Player_COMMAND_ID_REGISTER_MODULE;
        case BLT_DecoderServer_Message::COMMAND_ID_ADD_NODE:         return com_bluetune_player_Player_COMMAND_ID_ADD_NODE;
        case BLT_DecoderServer_Message::COMMAND_ID_SET_PROPERTY:     return com_bluetune_player_Player_COMMAND_ID_SET_PROPERTY;
        case BLT_DecoderServer_Message::COMMAND_ID_LOAD_PLUGIN:      return com_bluetune_player_Player_COMMAND_ID_LOAD_PLUGIN;
        case BLT_DecoderServer_Message::COMMAND_ID_LOAD_PLUGINS:     return com_bluetune_player_Player_COMMAND_ID_LOAD_PLUGINS;
        default:                                                     return -1;
    }
}

static jint MapDecoderState(BLT_DecoderServer::State state) 
{
    switch (state) {
        case BLT_DecoderServer::STATE_STOPPED:    return com_bluetune_player_Player_DECODER_STATE_STOPPED;
        case BLT_DecoderServer::STATE_PLAYING:    return com_bluetune_player_Player_DECODER_STATE_PLAYING;
        case BLT_DecoderServer::STATE_PAUSED:     return com_bluetune_player_Player_DECODER_STATE_PAUSED;
        case BLT_DecoderServer::STATE_EOS:        return com_bluetune_player_Player_DECODER_STATE_EOS;
        case BLT_DecoderServer::STATE_TERMINATED: return com_bluetune_player_Player_DECODER_STATE_TERMINATED;
        default:                                  return -1;
    }
}

class JniPlayer : public BLT_Player
{
public:
    NPT_IMPLEMENT_DYNAMIC_CAST_D(JniPlayer, BLT_Player)

    JniPlayer(jobject delegate) :
        m_JniEnv(NULL),
        m_Delegate(delegate),
        m_DelegateClass(NULL),
        m_DelegateMethod(NULL) {
    }
    
    ~JniPlayer() {
        if (m_JniEnv) {
            if (m_Delegate) {
                m_JniEnv->DeleteGlobalRef(m_Delegate);
            }
            if (m_DelegateClass) {
                m_JniEnv->DeleteGlobalRef(m_DelegateClass);
            } 
        }
    }
    
    void SetJniEnv(JNIEnv* env) { 
        if (env && env != m_JniEnv) {
            m_JniEnv = env;
            if (m_DelegateClass) {
                m_JniEnv->DeleteGlobalRef(m_DelegateClass);
                m_DelegateClass = NULL;
            }
            jclass messgage_handler_class = env->FindClass("com/bluetune/player/Player$MessageHandler");
            if (messgage_handler_class == NULL) {
                return;
            }
            m_DelegateClass = (jclass)env->NewGlobalRef(messgage_handler_class);
            m_DelegateMethod = env->GetMethodID(messgage_handler_class, "handleMessage", "(I[Ljava/lang/Object;[I)V");
        }
    }
    
    // event listener methods
    virtual void OnAckNotification(BLT_DecoderServer_Message::CommandId id) {
        ATX_LOG_FINE_1("ACK: %d", id);
        if (!m_JniEnv || !m_Delegate || !m_DelegateMethod) return;

        jintArray array = (jintArray)m_JniEnv->NewIntArray(1);
        jint value = MapCommandId(id);
        m_JniEnv->SetIntArrayRegion(array, 0, 1, &value);
        
        m_JniEnv->CallVoidMethod(m_Delegate, m_DelegateMethod, com_bluetune_player_Player_MESSAGE_TYPE_ACK, NULL, array);
    }

    virtual void OnNackNotification(BLT_DecoderServer_Message::CommandId id, BLT_Result result_code) {
        ATX_LOG_FINE_2("NACK: %d, result=%d", id, result_code);
        if (!m_JniEnv || !m_Delegate || !m_DelegateMethod) return;

        jintArray array = (jintArray)m_JniEnv->NewIntArray(2);
        jint values[2] = {MapCommandId(id), (jint)result_code};
        m_JniEnv->SetIntArrayRegion(array, 0, 2, values);
        
        m_JniEnv->CallVoidMethod(m_Delegate, m_DelegateMethod, com_bluetune_player_Player_MESSAGE_TYPE_NACK, NULL, array);
    }

    virtual void OnPongNotification(const void* cookie) {
        ATX_LOG_FINE_1("PONG: %x", (int)cookie);
        if (!m_JniEnv || !m_Delegate || !m_DelegateMethod) return;

        jintArray array = (jintArray)m_JniEnv->NewIntArray(1);
        jint value = (jint)cookie;
        m_JniEnv->SetIntArrayRegion(array, 0, 1, &value);

        m_JniEnv->CallVoidMethod(m_Delegate, m_DelegateMethod, com_bluetune_player_Player_MESSAGE_TYPE_PONG, NULL, array);
    }
    
    virtual void OnDecoderStateNotification(BLT_DecoderServer::State state) {
        ATX_LOG_FINE_1("DECODER-STATE: %d", (int)state);
        if (!m_JniEnv || !m_Delegate || !m_DelegateMethod) return;

        jintArray array = (jintArray)m_JniEnv->NewIntArray(1);
        jint value = MapDecoderState(state);
        m_JniEnv->SetIntArrayRegion(array, 0, 1, &value);
        
        m_JniEnv->CallVoidMethod(m_Delegate, m_DelegateMethod, com_bluetune_player_Player_MESSAGE_TYPE_DECODER_STATE, NULL, array);
    }
    
    virtual void OnDecoderEventNotification(BLT_DecoderServer::DecoderEvent& event) {
        ATX_LOG_FINE("DECODER-EVENT");
        if (!m_JniEnv || !m_Delegate || !m_DelegateMethod) return;

        jintArray array = NULL;
        switch (event.m_Type) {
            case BLT_DecoderServer::DecoderEvent::EVENT_TYPE_INIT_ERROR: {
                BLT_DecoderServer::DecoderEvent::ErrorDetails* details = (BLT_DecoderServer::DecoderEvent::ErrorDetails*)event.m_Details;
                array = (jintArray)m_JniEnv->NewIntArray(2);
                jint values[2] = { com_bluetune_player_Player_DECODER_EVENT_TYPE_INIT_ERROR, details->m_ResultCode };
                m_JniEnv->SetIntArrayRegion(array, 0, 2, values);
                break;
            }

            case BLT_DecoderServer::DecoderEvent::EVENT_TYPE_DECODING_ERROR: {
                BLT_DecoderServer::DecoderEvent::ErrorDetails* details = (BLT_DecoderServer::DecoderEvent::ErrorDetails*)event.m_Details;
                array = (jintArray)m_JniEnv->NewIntArray(2);
                jint values[2] = { com_bluetune_player_Player_DECODER_EVENT_TYPE_DECODER_ERROR, details->m_ResultCode };
                m_JniEnv->SetIntArrayRegion(array, 0, 2, values);
                break;
            }
        }
        
        m_JniEnv->CallVoidMethod(m_Delegate, m_DelegateMethod, com_bluetune_player_Player_MESSAGE_TYPE_DECODER_EVENT, NULL, array);
    }
    
    virtual void OnVolumeNotification(float volume) {
        ATX_LOG_FINE_1("VOLUME: %f", volume);
        if (!m_JniEnv || !m_Delegate || !m_DelegateMethod) return;

        jintArray array = (jintArray)m_JniEnv->NewIntArray(4);
        jint values[2] = {(jint)(volume*100.0f), 100};
        m_JniEnv->SetIntArrayRegion(array, 0, 2, values);

        m_JniEnv->CallVoidMethod(m_Delegate, m_DelegateMethod, com_bluetune_player_Player_MESSAGE_TYPE_VOLUME, NULL, array);
    }
    
    virtual void OnStreamTimeCodeNotification(BLT_TimeCode timecode) {
        ATX_LOG_FINE_4("TIMECODE: %02d:%02d:%02d:%02d", timecode.h, timecode.m, timecode.s, timecode.f);
        if (!m_JniEnv || !m_Delegate || !m_DelegateMethod) return;

        jintArray array = (jintArray)m_JniEnv->NewIntArray(4);
        jint values[4] = {(jint)timecode.h, (jint)timecode.m, (jint)timecode.s, (jint)timecode.f};
        m_JniEnv->SetIntArrayRegion(array, 0, 4, values);

        m_JniEnv->CallVoidMethod(m_Delegate, m_DelegateMethod, com_bluetune_player_Player_MESSAGE_TYPE_STREAM_TIMECODE, NULL, array);
    }
    
    virtual void OnStreamPositionNotification(BLT_StreamPosition& position) {
        ATX_LOG_FINE_2("STREAM-POSITION: %d/%d", (int)position.offset, (int)position.range);
        if (!m_JniEnv || !m_Delegate || !m_DelegateMethod) return;

        double fpos = 0.0;
        if (position.range) {
            fpos = (double)position.offset/(double)position.range;
        }
        jintArray array = (jintArray)m_JniEnv->NewIntArray(2);
        jint values[2] = {(jint)(fpos*10000.0), (jint)10000};
        m_JniEnv->SetIntArrayRegion(array, 0, 2, values);

        m_JniEnv->CallVoidMethod(m_Delegate, m_DelegateMethod, com_bluetune_player_Player_MESSAGE_TYPE_STREAM_POSITION, NULL, array);
    }
    
    virtual void OnStreamInfoNotification(BLT_Mask        update_mask, 
                                          BLT_StreamInfo& info) {
        ATX_LOG_FINE("STREAM-INFO");
        if (!m_JniEnv || !m_Delegate || !m_DelegateMethod) return;

        jintArray iarray = (jintArray)m_JniEnv->NewIntArray(16);
        jint ivalues[14] = {
            (jint)update_mask,
            (jint)info.mask, 
            (jint)info.type, 
            (jint)info.id, 
            (jint)info.nominal_bitrate,
            (jint)info.average_bitrate,
            (jint)info.instant_bitrate,
            (jint)(info.size/0x7FFFFFFFULL),
            (jint)(info.size%0x7FFFFFFFULL),
            (jint)(info.duration/0x7FFFFFFFULL),
            (jint)(info.duration%0x7FFFFFFFULL),
            (jint)info.sample_rate,
            (jint)info.channel_count,
            (jint)info.flags
        };
        m_JniEnv->SetIntArrayRegion(iarray, 0, 14, ivalues);

        jclass string_class = m_JniEnv->FindClass("java/lang/String");
        jobjectArray oarray = (jobjectArray)m_JniEnv->NewObjectArray(1, string_class, NULL);
        jobject ovalue;
        if (info.mask & BLT_STREAM_INFO_MASK_DATA_TYPE) {
            ovalue = m_JniEnv->NewStringUTF(info.data_type);
        } else {
            ovalue = m_JniEnv->NewStringUTF("");
        }
        m_JniEnv->SetObjectArrayElement(oarray, 0, ovalue);
        
        m_JniEnv->CallVoidMethod(m_Delegate, m_DelegateMethod, com_bluetune_player_Player_MESSAGE_TYPE_STREAM_INFO, oarray, iarray);
    }
    
    virtual void OnPropertyNotification(BLT_PropertyScope        scope,
                                        const char*              source,
                                        const char*              name,
                                        const ATX_PropertyValue* value) {
        ATX_LOG_FINE("PROPERTY");
        if (!m_JniEnv || !m_Delegate || !m_DelegateMethod) return;

        jint         ints[3]; // max 3
        unsigned int int_count = 2;
        switch (scope) {
            case BLT_PROPERTY_SCOPE_CORE:
                ints[0] = com_bluetune_player_Player_PROPERTY_SCOPE_CORE; break;
            case BLT_PROPERTY_SCOPE_STREAM:
                ints[0] = com_bluetune_player_Player_PROPERTY_SCOPE_STREAM; break;
            case BLT_PROPERTY_SCOPE_MODULE:
                ints[0] = com_bluetune_player_Player_PROPERTY_SCOPE_MODULE; break;
            default:
                ints[0] = -1;
        }
        
        jobject      strings[3]; // max 3
        unsigned int string_count = 2;
        if (value) {
            switch (value->type) {
                case ATX_PROPERTY_VALUE_TYPE_INTEGER:
                    ints[1] = com_bluetune_player_Player_PROPERTY_VALUE_TYPE_INTEGER;
                    ints[2] = (jint)value->data.integer;
                    ++int_count;
                    break;
                    
                case ATX_PROPERTY_VALUE_TYPE_STRING:
                    ints[1] = com_bluetune_player_Player_PROPERTY_VALUE_TYPE_STRING;
                    strings[2] = m_JniEnv->NewStringUTF(value->data.string);
                    ++string_count;
                    break;

                case ATX_PROPERTY_VALUE_TYPE_BOOLEAN:
                    ints[1] = com_bluetune_player_Player_PROPERTY_VALUE_TYPE_BOOLEAN;
                    ints[2] = (jint)value->data.boolean;
                    ++int_count;
                    break;

                default:
                    return; // ignore those types for now
            }
        } else {
            ints[1] = -1;
        }
        
        if (source) {
            strings[0] = m_JniEnv->NewStringUTF(source);
        } else {
            strings[0] = m_JniEnv->NewStringUTF("");
        }
        if (name) {
            strings[1] = m_JniEnv->NewStringUTF(name);
        } else {
            strings[1] = m_JniEnv->NewStringUTF("");
        }
        
        jclass string_class = m_JniEnv->FindClass("java/lang/String");
        jobjectArray oarray = (jobjectArray)m_JniEnv->NewObjectArray(string_count, string_class, NULL);
        for (unsigned int i=0; i<string_count; i++) {
            m_JniEnv->SetObjectArrayElement(oarray, i, strings[i]);
        }

        jintArray iarray = (jintArray)m_JniEnv->NewIntArray(int_count);
        m_JniEnv->SetIntArrayRegion(iarray, 0, int_count, ints);

        m_JniEnv->CallVoidMethod(m_Delegate, m_DelegateMethod, com_bluetune_player_Player_MESSAGE_TYPE_PROPERTY, oarray, iarray);
    }
    
private:
    JNIEnv*   m_JniEnv;
    jobject   m_Delegate;
    jclass    m_DelegateClass;
    jmethodID m_DelegateMethod;
};

NPT_DEFINE_DYNAMIC_CAST_ANCHOR(JniPlayer)

/*
 * Class:     com_bluetune_player_Player
 * Method:    _init
 * Signature: (Lcom/bluetune/player/Player/MessageHandler;)J
 */
JNIEXPORT jlong JNICALL 
Java_com_bluetune_player_Player__1init(JNIEnv *env, jclass, jobject handler)
{
    ATX_LOG_INFO("init");
    NPT_LOG_INFO("init");
    jobject delegate = env->NewGlobalRef(handler);
    JniPlayer* self = new JniPlayer(delegate);
    return (jlong)self;
}

/*
 * Class:     com_bluetune_player_Player
 * Method:    _pumpMessage
 * Signature: (J)I
 */
JNIEXPORT jint JNICALL 
Java_com_bluetune_player_Player__1pumpMessage(JNIEnv* env, jclass, jlong _self)
{
    JniPlayer* self = (JniPlayer*)_self;
        
    self->SetJniEnv(env);
    int result = self->PumpMessage();
    
    return result;
}

/*
 * Class:     com_bluetune_player_Player
 * Method:    _setInput
 * Signature: (JLjava/lang/String;Ljava/lang/String;)I
 */
JNIEXPORT jint JNICALL 
Java_com_bluetune_player_Player__1setInput(JNIEnv *env, jclass clazz, jlong _self, jstring _input, jstring _mimeType)
{
    JniPlayer* self = (JniPlayer*)_self;

    const char* input = env->GetStringUTFChars(_input, JNI_FALSE);
    if (input == NULL) return BLT_ERROR_INTERNAL;
    
    const char* mimeType = NULL;
    if (_mimeType){
        mimeType = env->GetStringUTFChars(_mimeType, JNI_FALSE);
    }

    ATX_LOG_FINE_2("SetInput %s, %s", input, mimeType?mimeType:"NULL");
    BLT_Result result = self->SetInput(input, mimeType);
    
    env->ReleaseStringUTFChars(_input, input);
    if (_mimeType && mimeType) env->ReleaseStringUTFChars(_mimeType, mimeType);
    return result;
}

/*
 * Class:     com_bluetune_player_Player
 * Method:    _setOutput
 * Signature: (JLjava/lang/String;Ljava/lang/String;)I
 */
JNIEXPORT jint JNICALL 
Java_com_bluetune_player_Player__1setOutput(JNIEnv* env, jclass, jlong _self, jstring _output, jstring _mimeType)
{
    JniPlayer* self = (JniPlayer*)_self;

    const char* output = env->GetStringUTFChars(_output, JNI_FALSE);
    if (output == NULL) return BLT_ERROR_INTERNAL;
    
    const char* mimeType = NULL;
    if (_mimeType){
        mimeType = env->GetStringUTFChars(_mimeType, JNI_FALSE);
    }

    ATX_LOG_FINE_2("SetOutput %s, %s", output, mimeType?mimeType:"NULL");
    BLT_Result result = self->SetOutput(output, mimeType);
    
    env->ReleaseStringUTFChars(_output, output);
    if (_mimeType && mimeType) env->ReleaseStringUTFChars(_mimeType, mimeType);
    return result;
}
  
/*
 * Class:     com_bluetune_player_Player
 * Method:    _play
 * Signature: (J)I
 */
JNIEXPORT jint JNICALL 
Java_com_bluetune_player_Player__1play(JNIEnv *, jclass, jlong _self)
{
    JniPlayer* self = (JniPlayer*)_self;
    
    return self->Play();
}

/*
 * Class:     com_bluetune_player_Player
 * Method:    _stop
 * Signature: (J)I
 */
JNIEXPORT jint JNICALL 
Java_com_bluetune_player_Player__1stop(JNIEnv *, jclass, jlong _self)
{
    JniPlayer* self = (JniPlayer*)_self;
    
    return self->Stop();
}

/*
 * Class:     com_bluetune_player_Player
 * Method:    _pause
 * Signature: (J)I
 */
JNIEXPORT jint JNICALL 
Java_com_bluetune_player_Player__1pause(JNIEnv *, jclass, jlong _self)
{
    JniPlayer* self = (JniPlayer*)_self;
    
    return self->Pause();
}

/*
 * Class:     com_bluetune_player_Player
 * Method:    _seekToTime
 * Signature: (JJ)I
 */
JNIEXPORT jint JNICALL 
Java_com_bluetune_player_Player__1seekToTime(JNIEnv *env, jclass, jlong _self, jlong time)
{
    JniPlayer* self = (JniPlayer*)_self;
    
    return self->SeekToTime(time);
}

/*
 * Class:     com_bluetune_player_Player
 * Method:    _seekToTimeStamp
 * Signature: (JIIII)I
 */
JNIEXPORT jint JNICALL 
Java_com_bluetune_player_Player__1seekToTimeStamp(JNIEnv *env, jclass, jlong _self, jint h, jint m, jint s, jint f)
{
    JniPlayer* self = (JniPlayer*)_self;
    
    return self->SeekToTimeStamp(h, m, s, f);
}
  
/*
 * Class:     com_bluetune_player_Player
 * Method:    _seekToPosition
 * Signature: (JJJ)I
 */
JNIEXPORT jint JNICALL 
Java_com_bluetune_player_Player__1seekToPosition(JNIEnv *, jclass, jlong _self, jlong position, jlong range)
{
    JniPlayer* self = (JniPlayer*)_self;
    
    return self->SeekToPosition(position, range);
}

/*
 * Class:     com_bluetune_player_Player
 * Method:    _setVolume
 * Signature: (JF)I
 */
JNIEXPORT jint JNICALL 
Java_com_bluetune_player_Player__1setVolume(JNIEnv *env, jclass, jlong _self, jfloat volume)
{
    JniPlayer* self = (JniPlayer*)_self;

    return self->SetVolume(volume);
}


