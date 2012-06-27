package com.bluetune.player;

public interface PlayerListener {
	public void onAck(int commandId);
	public void onNack(int commandId, int errorCode);
	public void onPong(int cookie);
	public void onDecoderStateChanged(int decoderState);
	public void onDecoderEvent();
	public void onVolumeChanged(float volume);
	public void onStreamTimeCode(int h, int m, int s, int f);
	public void onStreamPosition(float position);
	public void onStreamInfoChanged(int changeMask, StreamInfo streamInfo);
	public void onPropertyChanged(int scope, String source, String name, int type, Object value);
}
