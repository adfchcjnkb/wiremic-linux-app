cmake_minimum_required(VERSION 3.22)
project(wiremic_native LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# The wire protocol is the contract between the phone and the desktop. It used
# to be duplicated into this tree, and the copies drifted apart and broke the
# build; there is now one copy and both apps compile it.
get_filename_component(WIREMIC_SHARED
    "${CMAKE_CURRENT_SOURCE_DIR}/../../../../../shared" ABSOLUTE)

include(FetchContent)

FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.11.3
)
set(JSON_BuildTests OFF CACHE INTERNAL "")
FetchContent_MakeAvailable(nlohmann_json)

FetchContent_Declare(
    opus
    GIT_REPOSITORY https://github.com/xiph/opus.git
    GIT_TAG v1.4
)
set(OPUS_BUILD_SHARED_LIBRARY OFF CACHE INTERNAL "")
set(OPUS_BUILD_TESTING OFF CACHE INTERNAL "")
set(OPUS_BUILD_PROGRAMS OFF CACHE INTERNAL "")
FetchContent_MakeAvailable(opus)

find_package(openssl REQUIRED CONFIG)

add_library(wiremic_native SHARED
    jni/WireMicNative.cpp
    core/ConnectionManager.cpp
    network/DiscoveryService.cpp
    network/ControlClient.cpp
    audio/AudioSender.cpp
    audio/AudioCaptureBackend.cpp
    audio/AAudioCapture.cpp
    audio/OpenSLESCapture.cpp
    audio/OpusCodec.cpp
    audio/AudioPacketCodec.cpp
    audio/JitterBuffer.cpp
    ${WIREMIC_SHARED}/protocol/Protocol.cpp
    ${WIREMIC_SHARED}/protocol/MessageFraming.cpp
    security/CertificateManager.cpp
    security/TrustedDeviceStore.cpp
)

target_include_directories(wiremic_native PRIVATE
    ${WIREMIC_SHARED}/protocol
    ${WIREMIC_SHARED}/platform
    ${WIREMIC_SHARED}/audio
    security
    audio
    network
    core
)

target_link_libraries(wiremic_native PRIVATE
    nlohmann_json::nlohmann_json
    opus
    openssl::ssl
    openssl::crypto
    OpenSLES
    log
    android
)

target_compile_options(wiremic_native PRIVATE -Wall -Wextra -O2)
