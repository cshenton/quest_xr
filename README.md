# Quest 2 Native Minimal

This is a minimal repo to build an application for Quest 2 (Android, OpenXR) on the Windows
powershell.

- No IDE
- No build system
- No scripting languages (other than some trivial powershell path interpolation)

Just command line tool invocations, each explained. All the significant source code is in a single C++ file `src/main.cpp`.

![The working example](./quest_xr_example.jpg)

This repo is inspired by the work of `cnlohr` on [`tsopenxr`](https://github.com/cnlohr/tsopenxr). Much of the code
here is based on that example of how to build a Quest 2 apk just using the plain command line tools. However I've adapted the code to be all in one place, and adapted the build to no
longer depend on make, or tools like `zip` which are a bit awkward to get on windows.

## How to Build

First, ensure you have the relevant Android SDK, NDK, and build tools installed, the [official docs](https://developer.oculus.com/documentation/native/android/mobile-studio-setup-android/)
get this bit right, so just follow them. Make sure you actually open Android Studio, then open a blank project as
well, because these actually seems to be required to complete the installation... alternatively, there are the
plain [command line tools](https://developer.android.com/studio) (scroll down) but I haven't tested a fresh install
with those yet. 

The build process depends on the following executables:

- `aapt` the Android Asset Packaging Tool which ships with the Android SDK
- `adb` the Android Debug Bridge, used to install the apk
- `clang` the clang compiler that ships with the Android SDK (more specifically, the NDK)
- `jarsigner` in the Android JDK, used to sign our apk (instead of apksigner, since we're Android < 30)
- `keytool` in the Android JDK, used to generate a key store for signing
- `7z` the 7zip executable, available for download [here](https://www.7-zip.org/download.html)
- `zipalign` which is a zip alignment tool optimises the archive so it can be `mmap`ed, which ships with the Android SDK

### Paths

Let's define some paths to make the following commands somewhat readable. These paths
may be different for you, for example the JDK tools might be in `C:/Program Files/Android/Android Studio/jbr/bin`, and of course your username wont be `User`.

```powershell
# Shared paths
$ANDROID_SDK_HOME = "C:/Users/User/AppData/Local/Android/Sdk"
$ANDROID_JAR = "$ANDROID_SDK_HOME/platforms/android-29/android.jar"
$ANDROID_NDK_HOME = "$ANDROID_SDK_HOME/ndk/25.2.9519653"
$ANDROID_LLVM = "$ANDROID_NDK_HOME/toolchains/llvm/prebuilt/windows-x86_64"
$ANDROID_LIBS = "$ANDROID_LLVM/sysroot/usr/include"
$ANDROID_LIBS_LINK = "$ANDROID_LLVM/sysroot/usr/lib/aarch64-linux-android/29"

# Command line tools
$AAPT = "$ANDROID_SDK_HOME/build-tools/34.0.0/aapt"
$ADB = "$ANDROID_SDK_HOME/platform-tools/adb.exe"
$CLANG = "$ANDROID_LLVM/bin/clang"
$JARSIGNER = "C:/Program Files/Android/jdk/jdk-8.0.302.8-hotspot/jdk8u302-b08/bin/jarsigner.exe"
$KEYTOOL = "C:/Program Files/Android/jdk/jdk-8.0.302.8-hotspot/jdk8u302-b08/bin/keytool.exe"
$SZIP = "C:/Program Files/7-Zip/7z.exe"
$ZIPALIGN = "$ANDROID_SDK_HOME/build-tools/34.0.0/zipalign"
```

### Generate a debug key

The first time you run this, you'll want to generate an android debug key. Do that with keytool like:

```powershell
& $KEYTOOL -genkey -v -keystore debug.keystore -storepass android `
  -alias androiddebugkey -keypass android -keyalg RSA -keysize 2048 `
  -validity 10000 -dname "C=US, O=Android, CN=Android Debug"
```


### Create the build Directories

Now, create the directories we'll use for the build, ignoring errors if they already exist.

```powershell
mkdir -ea 0 build/assets
mkdir -ea 0 build/lib/arm64-v8a
```

### Compile the application

Compile the application with:

```powershell
& $CLANG --target=aarch64-linux-android29 -ffunction-sections -Os -fdata-sections `
 -Wall -fvisibility=hidden -m64 -Os -fPIC -DANDROIDVERSION=29 -DANDROID  `
 -Ideps/include -I./src -I$ANDROID_LIBS -I$ANDROID_LIBS/android `
 src/main.cpp deps/src/android_native_app_glue.c deps/lib/libopenxr_loader.so `
 -L$ANDROID_LIBS_LINK -s -lm -lGLESv3 -lEGL -landroid -llog `
 -shared -uANativeActivity_onCreate `
 -o build/lib/arm64-v8a/libquestxrexample.so
```

### Package, add application

Copy our assets into the build directory, package it with aapt. Then using `7z`, unzip it to
get the extra stuff from aapt, then zip it again with our compiled code included (yes it is 
as silly as it sounds).

```powershell
cp -ea 0 -r assets build
cp -ea 0 deps/lib/libopenxr_loader.so build/lib/arm64-v8a/

& $AAPT package -f -F temp.apk -I $ANDROID_JAR -M src/AndroidManifest.xml `
  -S resources -A build/assets -v --target-sdk-version 29

& $SZIP x temp.apk -obuild -aoa
rm -ea 0 -r build.apk
cd build
& $SZIP a -tzip -mx0 ../build.apk .
cd ..
```

### Sign and Align

Sign the application with `jarsigner` (not apksigner, that's an android 30+ thing), and run
zipalign on it.


```powershell
& $JARSIGNER -sigalg SHA1withRSA -digestalg SHA1 -verbose -keystore debug.keystore `
  -storepass android build.apk androiddebugkey

& $ZIPALIGN -f -v 4 build.apk questxrexample.apk
```

### Clean Up

Delete the build artifacts, don't delete the keystore!

```powershell
rm -ea 0 -r build
rm -ea 0 temp.apk
rm -ea 0 build.apk
```

## How to Run

To run, we'll use `adb` to install and run the apk. Make sure your Quest 2 is set up in
dev mode
[following the docs](https://developer.oculus.com/documentation/native/android/mobile-device-setup/) and plug it in, verify it's connected with:

```powershell
& $ADB devices
```

Then install the apk:

```powershell
& $ADB install -r questxrexample.apk
```

Now run it and run logcat with some filters to see what happens:

```powershell
& $ADB shell am start -n org.cshenton.questxrexample/android.app.NativeActivity
& $ADB logcat OpenXR:D questxrexample:D *:S -v color
```

Chuck on your headset and you should see a grid on the floor and some cubes on your controllers.
You may need to install/start again if it gets into a weird state.

## A list of commands... that's basically just a rubbish build system!

Yes that's the point, of _course_ you want some sort of build automation. But you probably
want _your_ build automation. The point of these commands is to make it transparent so you
can automate it for yourself! Whether that's using Make, CMake, or your own in-house build tool.

You can also port all of these commands to bash if you like, I just wanted something that
worked with native windows tools.

## How to Rename

You probably don't want my name all over your Quest 2 app. Here are all the locations you need to change. Implied
is that you need to change the string `cshenton` to your org name and `questxrexample` to your app name.

- `resources/values/strings.xml`
- `src/AndroidManifest.xml`
- `deps/src/android_native_app_glue.c`, yes powershell refused to not eat the quotes in `-DAPPNAME="questxrexample"` so I caved
- The above build "script"
  - `-o build/lib/arm64-v8a/libquestxrexample.so`
- The adb command

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

## Known Issues

- SDK `android_native_app_glue` didn't work
- Controllers appear to stop providing inputs on idle -> resume
- Load / Resume occasionally shows frame buffers in world space quads
