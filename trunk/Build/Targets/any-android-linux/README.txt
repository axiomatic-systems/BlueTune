This is not a real target, but a place to store common configuration data shared between all android targets

NDK Build:

When working on changes to Android.mk, it is usefule to be able to re-run ndk-build without causing a full rebuild. For this, use the following command (use only when experimenting with Android.mk, as compiler settings changes will be ignored)
'ndk-build -o jni/Android.mk'
