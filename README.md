# Quest 2 Native Minimal

This is a minimal repo to build an application for Quest 2 (Android, OpenXR) using:

- No IDE
- No build system
- No scripting languages

Just invocations of the C/C++ compiler and build tools that ship with the Android NDK.
I also thoroughly document the provenance of every vendored and hidden dependency, so that you
should be able to recreate the repo from scratch yourself.

All the significant source code is in a single C++ file `src/main.cpp`.

This repo is inspired by the work of `cnlohr` on [`tsopenxr`](https://github.com/cnlohr/tsopenxr). Much of the code
here is based on that example of how to build a Quest 2 apk just using the plain command line tools.

## Why?

I had a really rubbish time following the official docs for building native Quest Apps. They tell you to do things
that are incorrect, assume you're happy using a Java build system for your native game engine, and just generally
don't set you up for success in an ongoing project.

I wanted something that I could use, and point others to, as a good starting point to get a native Quest 2 apk
built from source with just C++, no Java build system, no accursed python code to generate the OpenXR headers,
no needless C++ abstractions between you and the OpenXR API calls that you need to learn.

This repo is C++, because that's the most common game engine language, but it is trivially portable to C 
(remove the `extern "C"` declarations and fix up the raw shaders strings, typedef the `app_t` struct).

## How to Build

The build process depends on the following executables:

- `keytool` in the Android JDK, used to generate a key store for signing
- `jarsigner` in the Android JDK, used to sign our apk (instead of apksigner, since we're Android < 30)
- `aarch64-linux-androidXX-clang` the clang cross-compiler that ships with the Android SDK (more specifically, the NDK)
- `aapt` the Android Asset Packaging Tool which ships with the Android SDK
- `zipalign` which is a zip alignment tool optimises the archive so it can be `mmap`ed, which ships with the Android SDK

It also depends on various headers in the NDK, all specified below. The following `bash` code outlines the build process

- Generate a key with `keytool`
- Make a directory for the build
- Compile our app as an `.so` with `aarch64-linux-androidXX-clang`
- Copy our assets into the build directory, then package with `aapt`
- Now unzip the generated `.apk` shove our `.so` files in there
- Sign it with `jarsigner`
- Align the zip archive with `zipalign`
- Clean up

```bash
# Build tool, header, source, and library locations, you may need to change these
JDK_KEYTOOL="C:/Program Files/Android/jdk/jdk-8.0.302.8-hotspot/jdk8u302-b08/bin/keytool.exe"
JDK_JARSIGNER="C:/Program Files/Android/jdk/jdk-8.0.302.8-hotspot/jdk8u302-b08/bin/jarsigner.exe"
ANDROID_SDK_HOME=C:/Users/User/AppData/Local/Android/Sdk
ANDROID_JAR=$ANDROID_SDK_HOME/platforms/android-29/android.jar
ANDROID_AAPT=$ANDROID_SDK_HOME/build-tools/34.0.0/aapt
ANDROID_ZIPALIGN=$ANDROID_SDK_HOME/build-tools/34.0.0/zipalign

ANDROID_NDK_HOME=$ANDROID_SDK_HOME/ndk/25.2.9519653
ANDROID_LLVM=$ANDROID_NDK_HOME/toolchains/llvm/prebuilt/windows-x86_64
ANDROID_CLANG=$ANDROID_LLVM/bin/aarch64-linux-android29-clang
ANDROID_LIBS=$ANDROID_LLVM/sysroot/usr/include
ANDROID_LIBS_LINK=$ANDROID_LLVM/sysroot/usr/lib/aarch64-linux-android/29

# Once, generate your key with keytool. Don't delete this, or put it in version control!
"$JDK_KEYTOOL" -genkey -v -keystore my-release-key.keystore -alias standkey -keyalg RSA -keysize 2048 -validity 10000 -storepass password -keypass password -dname "CN=example.com, OU=ID, O=Example, L=Doe, S=John, C=GB"

# Make build directories
mkdir -p build/assets
mkdir -p build/lib/arm64-v8a

# Compile binary
$ANDROID_CLANG -ffunction-sections -Os -fdata-sections -Wall -fvisibility=hidden -m64 -Os -fPIC \
  -DANDROIDVERSION=29 -DANDROID -DAPPNAME=\"questxrexample\" \
  -Ideps/include -I./src -I$ANDROID_LIBS -I-I$ANDROID_LIBS/android \
  src/main.cpp deps/src/android_native_app_glue.c deps/lib/libopenxr_loader.so \
  -L$ANDROID_LIBS_LINK -Wl,--gc-sections -s -lm -lGLESv3 -lEGL -landroid -llog -shared -uANativeActivity_onCreate \
  -o build/lib/arm64-v8a/libquestxrexample.so

# Copy assets, binaries into build directory and package with aapt
cp -r assets build
cp deps/lib/libopenxr_loader.so build/lib/arm64-v8a/
$ANDROID_AAPT package -f -F temp.apk -I $ANDROID_JAR -M src/AndroidManifest.xml -S resources -A build/assets -v --target-sdk-version 29

# Unzip the apk back into the build directory to pick up our binaries, then sign and rezip it
unzip -o temp.apk -d build
rm -rf build.apk
cd build && zip -D9r ../build.apk . && zip -D0r ../build.apk ./resources.arsc ./AndroidManifest.xml && cd ..
"$JDK_JARSIGNER" -sigalg SHA1withRSA -digestalg SHA1 -verbose -keystore my-release-key.keystore -storepass password build.apk standkey
$ANDROID_ZIPALIGN -f -v 4 build.apk questxrexample.apk

# Clean up
rm -rf temp.apk
rm -rf build.apk
rm -rf ./build
```

To run the build, plug in your developer mode enable quest 2. Then use `adb` to install and run the apk.

```bash
# Verify you're connected
adb devices

# Install the apk
adb install -r questxrexample.apk

# Run the apk
adb shell am start -n org.cshenton.questxrexample/android.app.NativeActivity && adb logcat OpenXR:D questxrexample:D *:S -v color
```

### A list of commands... that's basically just a rubbish build system!

Yes that's the point, of _course_ you want some sort of build automation. But you probably
want _your_ build automation. The point of these commands is to make it transparent so you
can automate it for yourself! Whether that's using Make, CMake, or your own in-house build tool.

## How to Rename

You probably don't want my name all over your Quest 2 app. Here are all the locations you need to change. Implied
is that you need to change the string `cshenton` to your org name and `questxrexample` to your app name.

- `resources/values/strings.xml`
- `src/AndroidManifest.xml`
- The above build "script"
  -  `-DAPPNAME=\"questxrexample\"`
  - `-o build/lib/arm64-v8a/libquestxrexample.so`
- The adb command
  - `adb shell am start -n org.cshenton.questxrexample/android.app.NativeActivity && adb logcat >> logs.txt`


## Vendor Provenance

Okay, but where did the vendored dependencies come from?

- From `ovr_openxr_mobile_sdk_55.0`
  - `deps/include/openxr` was copied from `3rdParty/khronos/openxr/OpenXR-SDK/include`
  - `deps/lib/libopenxr_loader.so` was copied from `OpenXR/Libs/Android/arm64-v8a/Release` 

- From `github.com/cnlohr/tsopenxr`
  - `deps/include/android_native_app_glue.h` was copied from `meta_quest/`
  - `deps/src/android_native_app_glue.c` was copied from `meta_quest/`

Why not just use the `android_native_app_glue` that ships with the NDK? Yeah good question. It didn't work, this one did.
For reference the headers are the same (save a `#pragma once` vs a regular header guard), I need to properly bisect the
source files to figure out the key difference.
