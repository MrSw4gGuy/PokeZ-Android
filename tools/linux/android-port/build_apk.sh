#!/bin/bash
# Assemble and sign the debug APK. Gradle only packages here - every .so was built by the Makefile already.
set -euo pipefail
WORK=${WORK:-/root/lasrecomp}
APP=$WORK/build/android/app
export ANDROID_HOME=${ANDROID_HOME:-/opt/android-sdk}
export ANDROID_SDK_ROOT=$ANDROID_HOME
export JAVA_HOME=${JAVA_HOME:-/usr/lib/jvm/java-17-openjdk-amd64}

cd "$APP"

# AGP 8.7 needs Gradle 8.9+; SDL's template wrapper may be older, so pin it rather than find out at the end
W=gradle/wrapper/gradle-wrapper.properties
echo "wrapper was: $(grep distributionUrl "$W" | sed 's/.*gradle-//; s/-bin.*//')"
sed -i 's|distributionUrl=.*|distributionUrl=https\\://services.gradle.org/distributions/gradle-8.11.1-bin.zip|' "$W"

echo "=== assembleDebug ==="
./gradlew --no-daemon assembleDebug 2>&1 | tail -25

APK=$(find "$APP/app/build/outputs/apk" -name '*.apk' | head -1)
[ -n "$APK" ] || { echo "no APK produced"; exit 1; }

echo
echo "=== APK ==="
ls -lh "$APK" | sed 's/.* \([0-9.]*[MK]\) /size: \1  /'
"$ANDROID_HOME/build-tools/35.0.0/apksigner" verify --print-certs "$APK" 2>&1 | head -4 || true
echo
echo "=== what is inside ==="
unzip -l "$APK" | grep -E "lib/arm64-v8a|classes.dex" | sort -k1 -n -r | head -12
echo
echo "APK: $APK"
