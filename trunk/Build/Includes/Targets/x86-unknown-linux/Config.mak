#######################################################################
#
#    target configuration
#
#    (c) 2002-2003 Gilles Boccon-Gibod
#    Author: Gilles Boccon-Gibod (bok@bok.net)
#
#######################################################################
BLT_PLUGINS = \
	FileInput 		\
	AlsaOutput		\
	MpegAudioDecoder\
	StreamPacketizer\
	CddaInput 		\
	NullOutput		\
	FileOutput		\
	PacketStreamer	\
	TagParser		\
	FlacDecoder		\
	VorbisDecoder	\
	DebugOutput		\
	WaveParser 		\
	AiffParser		\
	WaveFormatter	\
	SilenceRemover	\
	GainControlFilter \
	PcmAdapter		\
	OssOutput		\
	AlsaInput		\
#	CrossFader		\

BLT_FLAGS_C += -DBLT_CONFIG_MODULES_DEFAULT_OUTPUT_NAME=\"alsa:default\"
