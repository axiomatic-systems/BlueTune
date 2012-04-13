/*****************************************************************
|
|   BlueTune - Player
|
|   (c) 2002-2012 Gilles Boccon-Gibod
|   Author: Gilles Boccon-Gibod (bok@bok.net)
|
 ****************************************************************/

package com.bluetune.player;

public class Player {
	public Player(PlayerListener listener) {
		this.listener = listener;
		cSelf = _init(listener);
	}
	
	public void setInput(String input) {
		_setInput(cSelf, input);
	}
	
	public void setOutput(String output) {
		_setOutput(cSelf, output);
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

	// C glue
	private static native long _init(PlayerListener listener);
	private static native int _setInput(long self, String input);
	private static native int _setOutput(long self, String output);
	private static native int _play(long self);
	private static native int _stop(long self);
	private static native int _pause(long self);
	private static native int _seekToTime(long self, long time);
	private static native int _seekToTimeStamp(long self, int h, int m, int s, int f);
	private static native int _seekToPosition(long self, long offset, long range);
	private static native int _setVolume(long self, float volume);
	private final long cSelf;
	private PlayerListener listener;
	
	static {
        System.loadLibrary("bluetune-jni");
    }
}

