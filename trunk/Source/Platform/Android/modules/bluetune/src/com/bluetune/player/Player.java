/*****************************************************************
|
|   BlueTune - Player
|
|   (c) 2002-2012 Gilles Boccon-Gibod
|   Author: Gilles Boccon-Gibod (bok@bok.net)
|
 ****************************************************************/

package com.bluetune.player;

import android.os.Handler;


public class Player {
	public static final int COMMAND_ID_SET_INPUT        = 0;
	public static final int COMMAND_ID_SET_OUTPUT       = 1;
	public static final int COMMAND_ID_SET_VOLUME       = 2;
	public static final int COMMAND_ID_PLAY             = 3;
	public static final int COMMAND_ID_STOP             = 4;
	public static final int COMMAND_ID_PAUSE            = 5;
	public static final int COMMAND_ID_PING             = 6;
	public static final int COMMAND_ID_SEEK_TO_TIME     = 7;
	public static final int COMMAND_ID_SEEK_TO_POSITION = 8;
	public static final int COMMAND_ID_REGISTER_MODULE  = 9;
	public static final int COMMAND_ID_ADD_NODE         = 10;
	public static final int COMMAND_ID_SET_PROPERTY     = 11;
	public static final int COMMAND_ID_LOAD_PLUGIN      = 12;
	public static final int COMMAND_ID_LOAD_PLUGINS     = 13;
	
    public static final int MESSAGE_TYPE_ACK             = 0;
    public static final int MESSAGE_TYPE_NACK            = 1;
    public static final int MESSAGE_TYPE_PONG            = 2;
    public static final int MESSAGE_TYPE_DECODER_STATE   = 3;
    public static final int MESSAGE_TYPE_DECODER_EVENT   = 4;
    public static final int MESSAGE_TYPE_VOLUME          = 5;
    public static final int MESSAGE_TYPE_STREAM_TIMECODE = 6;
    public static final int MESSAGE_TYPE_STREAM_POSITION = 7;
    public static final int MESSAGE_TYPE_STREAM_INFO     = 8;
    public static final int MESSAGE_TYPE_PROPERTY        = 9;
    
    public static final int DECODER_STATE_STOPPED    = 0;
    public static final int DECODER_STATE_PLAYING    = 1;
    public static final int DECODER_STATE_PAUSED     = 2;
    public static final int DECODER_STATE_EOS        = 3;
    public static final int DECODER_STATE_TERMINATED = 4;

    public static final int DECODER_EVENT_TYPE_INIT_ERROR    = 0;
    public static final int DECODER_EVENT_TYPE_DECODER_ERROR = 1;
    
    public static final int PROPERTY_SCOPE_CORE   = 0;
    public static final int PROPERTY_SCOPE_STREAM = 1;
    public static final int PROPERTY_SCOPE_MODULE = 2;
    
    public static final int PROPERTY_VALUE_TYPE_INTEGER = 0;
    public static final int PROPERTY_VALUE_TYPE_FLOAT   = 1;
    public static final int PROPERTY_VALUE_TYPE_STRING  = 2;
    public static final int PROPERTY_VALUE_TYPE_BOOLEAN = 3;
    
	private class MessageHandler {
		public MessageHandler(PlayerListener listener, Handler handler) {
			this.listener = listener;
			this.handler  = handler;
		}
		
		public void deliverMessage(final int messageType, final Object[] oArgs, final int[] iArgs) {
			switch (messageType) {
				case MESSAGE_TYPE_ACK:
					listener.onAck(iArgs[0]);
					break;
					
				case MESSAGE_TYPE_NACK:
					listener.onNack(iArgs[0], iArgs[1]);
					break;
					
				case MESSAGE_TYPE_PONG:
					listener.onPong(iArgs[0]);
					break;
					
				case MESSAGE_TYPE_DECODER_STATE:
					listener.onDecoderStateChanged(iArgs[0]);
					break;
					
				case MESSAGE_TYPE_STREAM_TIMECODE:
					listener.onStreamTimeCode(iArgs[0], iArgs[1], iArgs[2], iArgs[3]);
					break;

				case MESSAGE_TYPE_STREAM_POSITION:
					listener.onStreamPosition((float)iArgs[0]/10000.0f);
					break;

				case MESSAGE_TYPE_STREAM_INFO:
					StreamInfo info = new StreamInfo(iArgs[1],
							                         (String)oArgs[0],
							                         iArgs[2],
							                         iArgs[3],
							                         iArgs[4],
							                         iArgs[5],
							                         iArgs[6],
							                         iArgs[7]*0x7FFFFFFF+iArgs[ 8],
							                         iArgs[9]*0x7FFFFFFF+iArgs[10],
							                         iArgs[11],
							                         iArgs[12],
							                         iArgs[13]);
					listener.onStreamInfoChanged(iArgs[0], info);
					break;

				case MESSAGE_TYPE_PROPERTY:
					Object value = null;
					switch (iArgs[1]) {
					case PROPERTY_VALUE_TYPE_INTEGER:
						value = new Integer(iArgs[2]);
						break;
						
					case PROPERTY_VALUE_TYPE_BOOLEAN:
						value = new Boolean(iArgs[2] != 0);
						break;
						
					case PROPERTY_VALUE_TYPE_STRING:
						value = oArgs[2];
						
					case -1:
						break;
						
					default: 
						return;
					}
					listener.onPropertyChanged(iArgs[0], (String)oArgs[0], (String)oArgs[1], iArgs[1], value);
					break;
					
				case MESSAGE_TYPE_VOLUME:
					listener.onVolumeChanged((float)iArgs[0]/100.0f);
					break;
				}
		}

		@SuppressWarnings("unused")
		public void handleMessage(final int messageType, final Object[] oArgs, final int[] iArgs) {
			System.out.print("MSG: type=" + messageType);
			if (handler == null) {
				deliverMessage(messageType, oArgs, iArgs);
			} else {
				handler.post(new Runnable() {
					public void run() {
						deliverMessage(messageType, oArgs, iArgs);
					}
				});
			}
		}

		private PlayerListener listener;
		private Handler        handler;
	}

	private class MessageLoop extends Thread {
		public MessageLoop(Player player) {
			this.player = player;
		}
		
	    public void run() {
    		try {
    			while (true) {
					player.pumpMessage();
				}
    		} catch (Exception e) {
    			System.out.println("End Of Loop");
	    	}
	    }

	    private Player player;
	}
	
	public Player(PlayerListener listener, Handler handler) {
		messageHandler = new MessageHandler(listener, handler);
		cSelf = _init(messageHandler);
		messageLoop = new MessageLoop(this);
		messageLoop.start();
	}

	public Player(PlayerListener listener) {
		this(listener, null);
	}
	
	public void pumpMessage() throws Exception {
		int result = _pumpMessage(cSelf);
		if (result != 0) {
			throw new Exception();
		}
	}
	
	public void setInput(String input, String mimeType) {
		_setInput(cSelf, input, mimeType);
	}
	
	public void setInput(String input) {
		setInput(input, null);
	}

	public void setInput(Input input, String mimeType) {
		_setInput(cSelf, input, mimeType);
	}

	public void setInput(Input input) {
		setInput(input, null);
	}

	public void setOutput(String output, String mimeType) {
		_setOutput(cSelf, output, mimeType);
	}

	public void setOutput(String output) {
		setOutput(output, null);
	}

	public void play() {
		_play(cSelf);
	}
	
	public void stop() {
		_stop(cSelf);
	}

	public void pause() {
		_pause(cSelf);
	}

	public void seekToTime(long time) {
		_seekToTime(cSelf, time);
	}
	
	public void seekToTimeStamp(int h, int m, int s, int f) {
		_seekToTimeStamp(cSelf, h,m,s,f);
	}
	
	public void seekToPosition(long offset, long range) {
		_seekToPosition(cSelf, offset, range);
	}

	public void setVolume(float volume) {
		_setVolume(cSelf, volume);
	}

	public void setProperty(int scope, String name, int value) {
		_setPropertyInteger(cSelf, scope, name, value);
	}

	public void setProperty(int scope, String name, String value) {
		_setPropertyString(cSelf, scope, name, value);
	}

	// C glue
	private static native long _init(MessageHandler messageHandler);
	private static native int _pumpMessage(long self);
	private static native int _setInput(long self, String input, String mimeType);
	private static native int _setInput(long self, Input input, String mimeType);
	private static native int _setOutput(long self, String output, String mimeType);
	private static native int _play(long self);
	private static native int _stop(long self);
	private static native int _pause(long self);
	private static native int _seekToTime(long self, long time);
	private static native int _seekToTimeStamp(long self, int h, int m, int s, int f);
	private static native int _seekToPosition(long self, long offset, long range);
	private static native int _setVolume(long self, float volume);
	private static native int _setPropertyInteger(long self, int scope, String name, int value);
	private static native int _setPropertyString(long self, int scope, String name, String value);
	private final long     cSelf;
	private MessageHandler messageHandler;
	private MessageLoop    messageLoop;
	
	static {
        System.loadLibrary("bluetune-jni");
    }
}

