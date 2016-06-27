The universal-apple-macosx target was compiled like this (from an ffmpeg snapshot taken Mar 16 2011):
./configure --disable-stripping --extra-cflags='-arch i386' --extra-ldflags='-arch i386' --disable-encoders --disable-decoders --enable-decoder=h264 --disable-parsers --enable-parser=h264 --disable-avformat --disable-protocols --disable-muxers --disable-bsfs --disable-demuxers --disable-filters --enable-shared
Then, manually edit config.h to change HAVE_POSIX_MEMALIGN from 1 to 0

other build:
ffmpeg-2.5.4 
./configure --disable-stripping --disable-encoders --disable-decoders --enable-decoder=h264 --enable-decoder=hevc --disable-parsers --enable-parser=h264 --enable-parser=hevc --disable-avformat --disable-protocols --disable-muxers --disable-bsfs --disable-demuxers --disable-filters --disable-ffplay --disable-ffprobe --disable-ffserver 
