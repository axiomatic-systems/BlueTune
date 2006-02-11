#######################################################################
#
#    Target configuration
#
#    (c) 2002-2003 Gilles Boccon-Gibod
#    Author: Gilles Boccon-Gibod (bok@bok.net)
#
#######################################################################
BLT_PLUGINS = FileInput CddaInput WaveParser GenericParser TagParser MpegAudioDecoder FlacDecoder VorbisDecoder FileOutput DebugOutput NeutrinoOutput
BLT_FLAGS_C += -DBLT_CONFIG_MODULES_DEFAULT_OUTPUT_NAME=\"alsa:0\"
