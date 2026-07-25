# WireMic Android

## Architecture

All connection, discovery, security, and audio logic lives in C++ under
`app/src/main/cpp`, built via the NDK and exposed to Kotlin through a single
JNI bridge (`core/NativeBridge.kt`). Kotlin only handles UI (Jetpack Compose,
Material 3), permissions, and the foreground service required to keep
streaming while the screen is off.

```
cpp/
  protocol/    same wire protocol as the Linux app (copied, Qt-free)
  security/    same certificate + trusted-device code as the Linux app
  audio/       same Opus + ChaCha20-Poly1305 codec as the Linux app
  network/     Android-specific: POSIX sockets + OpenSSL instead of Qt
  core/        ConnectionManager (initiator-only: discovery + connect + stream)
  jni/         WireMicNative.cpp — the only Kotlin/C++ boundary
```

`protocol/`, `security/`, and `audio/` are byte-for-byte the same files used
by the Linux desktop app (they never depended on Qt), so both apps always
speak the identical wire protocol.

Android only ever initiates a connection and only ever sends audio — it has
no `ControlServer` and no `AudioReceiver`. Whichever computer accepts the
request negotiates the `AudioSession` and becomes the one creating the
virtual microphone.

Audio capture uses the AAudio NDK API directly in C++
(`audio/AudioSender.cpp`): the capture callback runs entirely native,
encoding with Opus and encrypting each packet before sending UDP, with no
round-trip through Kotlin per audio frame.

## Building

Requires Android Studio (Koala or newer) with NDK 26.3.11579264 and CMake
3.22+. The native build fetches `nlohmann_json` and `opus` from their
official GitHub repositories via CMake `FetchContent`, and OpenSSL via the
prebuilt `com.android.ndk.thirdparty:openssl` Prefab package — both require
network access during the Gradle sync/build, same as any normal Android
project with native dependencies.

```
./gradlew assembleDebug
./gradlew bundleRelease
```

## Known limitation of this delivery

This native (NDK/C++) side could not be compile-tested in the environment
this code was generated in, because it has no network access to Google's
Android SDK/NDK servers. The Linux desktop code in this same repository
*was* fully compiled and test-executed (see `docs/PROTOCOL.md` and the
`linux-app` test suite) — the Android C++ files reuse that exact
already-verified logic where possible (protocol, security, audio codec),
and the new Android-specific files (POSIX/OpenSSL networking, AAudio
capture, JNI bridge) follow the standard NDK/OpenSSL/AAudio APIs but have
only been reviewed, not compiled. Build them in Android Studio first and
expect to fix minor issues (a missing include, a Prefab path, etc.) the way
any first NDK build usually needs.
