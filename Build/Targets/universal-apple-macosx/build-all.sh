# Base SDK
for CONFIG in Debug Release; do
    xcodebuild -project BlueTune.xcodeproj -scheme All -configuration $CONFIG -arch x86_64 only_active_arch=no -sdk "macosx" -derivedDataPath out
done

# WMA extension
if [ -d ../../../ThirdParty/WMSDK ]; then
    for CONFIG in Debug Release; do
        for SCHEME in WMSDK BltWmaDecoder; do
            xcodebuild -project BlueTune.xcodeproj -scheme $SCHEME -configuration $CONFIG -arch arm64 -arch armv7 -arch armv7s only_active_arch=no -sdk "iphoneos" -derivedDataPath out
            xcodebuild -project BlueTune.xcodeproj -scheme $SCHEME -configuration $CONFIG -arch x86_64 only_active_arch=no defines_module=yes -sdk "iphonesimulator" -derivedDataPath out
            xcodebuild -project BlueTune.xcodeproj -scheme $SCHEME -configuration $CONFIG -arch x86_64 only_active_arch=no defines_module=yes -sdk "macosx" -derivedDataPath out
        done
    done
fi
