#!/bin/bash
# The Android SDK pieces Gradle needs to assemble an APK. The NDK is already in /opt/android-ndk and is not
# reinstalled here: this is only the command-line tools, a platform to compile the Java against, and build-tools
# (aapt2, zipalign, apksigner).
set -euo pipefail
SDK=${SDK:-/opt/android-sdk}
CLI_VER=${CLI_VER:-13114758}   # commandlinetools-linux-<build>_latest.zip
PLATFORM=${PLATFORM_API:-35}
BUILDTOOLS=${BUILDTOOLS:-35.0.0}

export ANDROID_HOME=$SDK
export ANDROID_SDK_ROOT=$SDK

if [ ! -x "$SDK/cmdline-tools/latest/bin/sdkmanager" ]; then
    echo "=== command-line tools ==="
    mkdir -p "$SDK/cmdline-tools"
    curl -fL --progress-bar -o /root/cmdline-tools.zip \
        "https://dl.google.com/android/repository/commandlinetools-linux-${CLI_VER}_latest.zip"
    rm -rf /root/cmdline-unzip && mkdir -p /root/cmdline-unzip
    unzip -q /root/cmdline-tools.zip -d /root/cmdline-unzip
    rm -rf "$SDK/cmdline-tools/latest"
    mv /root/cmdline-unzip/cmdline-tools "$SDK/cmdline-tools/latest"
    rm -rf /root/cmdline-unzip /root/cmdline-tools.zip
fi

SM=$SDK/cmdline-tools/latest/bin/sdkmanager
echo "=== accepting licences ==="
yes | "$SM" --licenses > /dev/null 2>&1 || true

echo "=== platform-tools, platform $PLATFORM, build-tools $BUILDTOOLS ==="
"$SM" --install "platform-tools" "platforms;android-$PLATFORM" "build-tools;$BUILDTOOLS" 2>&1 | tail -3

echo
echo "=== installed ==="
ls "$SDK"
"$SDK/build-tools/$BUILDTOOLS/apksigner" --version 2>/dev/null | sed 's/^/apksigner /' || true
echo "SDK OK at $SDK"
