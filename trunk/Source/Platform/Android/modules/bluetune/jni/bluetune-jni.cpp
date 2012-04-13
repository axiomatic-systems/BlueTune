#include <assert.h>
#include <jni.h>
#include <string.h>
#include <sys/types.h>

#include "bluetune-jni.h"
#include "BlueTune.h"

__attribute__((constructor)) static void onDlOpen(void)
{
}

/*
 * Class:     com_bluetune_player_Player
 * Method:    _init
 * Signature: ()J
 */JNIEXPORT jlong JNICALL 
Java_com_bluetune_player_Player__1init(JNIEnv *, jclass, jobject listener)
{
    BLT_Player* self = new BLT_Player();

    return (jlong)self;
}

/*
 * Class:     com_bluetune_player_Player
 * Method:    _setInput
 * Signature: (JLjava/lang/String;)I
 */
JNIEXPORT jint JNICALL 
Java_com_bluetune_player_Player__1setInput(JNIEnv *env, jclass clazz, jlong _self, jstring input)
{
}

/*
 * Class:     com_bluetune_player_Player
 * Method:    _setOutput
 * Signature: (JLjava/lang/String;)I
 */
JNIEXPORT jint JNICALL 
Java_com_bluetune_player_Player__1setOutput(JNIEnv *, jclass, jlong, jstring)
{
}
  
/*
 * Class:     com_bluetune_player_Player
 * Method:    _play
 * Signature: (J)I
 */
JNIEXPORT jint JNICALL 
Java_com_bluetune_player_Player__1play(JNIEnv *, jclass, jlong _self)
{
    BLT_Player* self = (BLT_Player*)_self;
    
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
    BLT_Player* self = (BLT_Player*)_self;
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
    BLT_Player* self = (BLT_Player*)_self;
    return self->Pause();
}




