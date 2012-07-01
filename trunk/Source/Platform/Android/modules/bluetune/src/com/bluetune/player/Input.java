package com.bluetune.player;

public interface Input {
	public static final int ERROR_EOS           = -1; 
	public static final int ERROR_NOT_SUPPORTED = -2; 
	public static final int ERROR_OUT_OF_RANGE  = -3;
	
	public int read(byte[] buffer, int byteCount);
	public int seek(long offset);
	public long tell();
	public long getSize();
	public long getAvailable();
}
