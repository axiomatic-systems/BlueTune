#######################################################################
#
#    target configuration
#
#    (c) 2002-2003 Gilles Boccon-Gibod
#    Author: Gilles Boccon-Gibod (bok@bok.net)
#
#######################################################################
#BLT_PLUGINS = \
#	FileInput 		\
#	OssOutput		\
#	MpegAudioDecoder	\
#	StreamPacketizer	\
#	CddaInput 		\
#	NullOutput		\
#	FileOutput		\
#	PacketStreamer		\
#	TagParser		\
#	FlacDecoder		\
#	VorbisDecoder		\
#	DebugOutput		\
#	WaveParser 		\
#	WaveFormatter		\
#	SilenceRemover		\
#	GainControlFilter       \
#	CrossFader		
BLT_PLUGINS = \
	FileInput 		\
	Win32Output		\
	MpegAudioDecoder	\
	StreamPacketizer	\
	NullOutput		\
	FileOutput		\
	PacketStreamer		\
	TagParser		\
	DebugOutput		\
	WaveParser 		\
	WaveFormatter		\
	SilenceRemover		\
	GainControlFilter       \
	CrossFader		\

BLT_FLAGS_C += -DBLT_CONFIG_MODULES_DEFAULT_OUTPUT_NAME=\"wave:0\"
