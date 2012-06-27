package com.bluetune.player;

public class StreamInfo {
	public final static int STREAM_INFO_MASK_TYPE       	  = 0x0001;
	public final static int STREAM_INFO_MASK_ID              = 0x0002;
	public final static int STREAM_INFO_MASK_NOMINAL_BITRATE = 0x0004;
	public final static int STREAM_INFO_MASK_AVERAGE_BITRATE = 0x0008;
	public final static int STREAM_INFO_MASK_INSTANT_BITRATE = 0x0010;
	public final static int STREAM_INFO_MASK_SIZE            = 0x0020;
	public final static int STREAM_INFO_MASK_DURATION        = 0x0040;
	public final static int STREAM_INFO_MASK_SAMPLE_RATE     = 0x0080;
	public final static int STREAM_INFO_MASK_CHANNEL_COUNT   = 0x0100;
	public final static int STREAM_INFO_MASK_WIDTH           = 0x0200;
	public final static int STREAM_INFO_MASK_HEIGHT          = 0x0400;
	public final static int STREAM_INFO_MASK_FLAGS           = 0x0800;
	public final static int STREAM_INFO_MASK_DATA_TYPE       = 0x1000;
	
	public StreamInfo(int    mask,
			          String dataType,
			          int    streamType,
			          int    id,
			          int    nominalBitrate,
			          int    averageBitrate,
			          int    instantBitrate,
			          long   size,
			          long   duration,
			          int    sampleRate,
			          int    channelCount,
			          int    flags) {
		this.mask           = mask;
		this.dataType       = dataType;
		this.streamType     = streamType;
		this.id             = id;
		this.nominalBitrate = nominalBitrate;
		this.averageBitrate = averageBitrate;
		this.instantBitrate = instantBitrate;
		this.size           = size;
		this.duration       = duration;
		this.sampleRate     = sampleRate;
		this.channelCount   = channelCount;
		this.flags          = flags;
	}
	
	public int    mask;
	public String dataType;
	public int    streamType;
	public int    id;
	public int    nominalBitrate;
	public int    averageBitrate;
	public int    instantBitrate;
	public long   size;
	public long   duration;
	public int    sampleRate;
	public int    channelCount;
	public int    flags;
}
