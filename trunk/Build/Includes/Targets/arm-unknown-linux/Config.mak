#######################################################################
#
#    target configuration
#
#    (c) 2002-2003 Gilles Boccon-Gibod
#    Author: Gilles Boccon-Gibod (bok@bok.net)
#
#######################################################################
BLT_PLUGINS = FileInput CddaInput GenericParser WaveParser TagParser MpegAudioDecoder OssOutput NullOutput FileOutput DebugOutput StreamPacketizer PacketStreamer
BLT_FLAGS_C += -DBLT_CONFIG_MODULES_DEFAULT_OUTPUT_NAME=\"oss:0\"
FLO_DECODER_ENGINE = ffmpeg
