find_package(Qt6 REQUIRED COMPONENTS Core Network)
find_package(OpenSSL REQUIRED)

set(CMAKE_AUTOMOC ON)

find_package(nlohmann_json 3.11 REQUIRED)

add_library(wiremic_security STATIC
    src/core/security/CertificateManager.cpp
    src/core/security/TrustedDeviceStore.cpp
)

target_include_directories(wiremic_security PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core/security
)

target_link_libraries(wiremic_security PUBLIC
    OpenSSL::SSL
    OpenSSL::Crypto
    nlohmann_json::nlohmann_json
)
target_compile_features(wiremic_security PUBLIC cxx_std_20)

if(WIREMIC_BUILD_TESTS)
    add_executable(wiremic_certificate_test src/core/security/tests/certificate_manager_test.cpp)
    target_link_libraries(wiremic_certificate_test PRIVATE wiremic_security wiremic_testing)
    add_test(NAME certificate_manager COMMAND wiremic_certificate_test)

    add_executable(wiremic_trusted_device_test src/core/security/tests/trusted_device_store_test.cpp)
    target_link_libraries(wiremic_trusted_device_test PRIVATE wiremic_security wiremic_testing)
    add_test(NAME trusted_device_store COMMAND wiremic_trusted_device_test)
endif()


add_library(wiremic_network STATIC
    src/network/DiscoveryService.cpp
    src/network/ControlServer.cpp
    src/network/ControlClient.cpp
)

target_include_directories(wiremic_network PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src/network
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core/security
)

target_link_libraries(wiremic_network PUBLIC
    Qt6::Core
    Qt6::Network
    wiremic_protocol
    wiremic_security
)

target_compile_features(wiremic_network PUBLIC cxx_std_20)

if(WIREMIC_BUILD_TESTS)
    add_executable(wiremic_discovery_test src/network/tests/discovery_smoke_test.cpp)
    target_link_libraries(wiremic_discovery_test PRIVATE wiremic_network wiremic_testing)
    add_test(NAME discovery_smoke COMMAND wiremic_discovery_test)

    add_executable(wiremic_control_channel_test src/network/tests/control_channel_loopback_test.cpp)
    target_link_libraries(wiremic_control_channel_test PRIVATE wiremic_network wiremic_testing)
    add_test(NAME control_channel_loopback COMMAND wiremic_control_channel_test)
endif()

find_package(PkgConfig REQUIRED)
pkg_check_modules(PIPEWIRE REQUIRED IMPORTED_TARGET libpipewire-0.3)
pkg_check_modules(PULSE REQUIRED IMPORTED_TARGET libpulse-simple libpulse)

add_library(wiremic_platform STATIC
    src/platform/PipeWireVirtualMic.cpp
    src/platform/PipeWireAudioCapture.cpp
    src/platform/AudioDeviceLister.cpp
    src/platform/PulseAudioVirtualMic.cpp
    src/platform/VirtualMicBackend.cpp
)

target_include_directories(wiremic_platform PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src/platform
)

target_link_libraries(wiremic_platform PUBLIC
    PkgConfig::PIPEWIRE
    PkgConfig::PULSE
)

target_compile_features(wiremic_platform PUBLIC cxx_std_20)

if(WIREMIC_BUILD_TESTS)
    add_executable(wiremic_virtual_mic_test src/platform/tests/virtual_mic_test.cpp)
    target_link_libraries(wiremic_virtual_mic_test PRIVATE wiremic_platform wiremic_testing)
    add_test(NAME virtual_mic COMMAND wiremic_virtual_mic_test)

    add_executable(wiremic_audio_capture_test src/platform/tests/audio_capture_test.cpp)
    target_link_libraries(wiremic_audio_capture_test PRIVATE wiremic_platform wiremic_testing)
    add_test(NAME audio_capture COMMAND wiremic_audio_capture_test)
endif()
pkg_check_modules(OPUS REQUIRED IMPORTED_TARGET opus)

add_library(wiremic_audio STATIC
    src/audio/AudioPacketCodec.cpp
    src/audio/OpusCodec.cpp
    src/audio/JitterBuffer.cpp
    src/audio/AudioSender.cpp
    src/audio/AudioReceiver.cpp
)

target_include_directories(wiremic_audio PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src/audio
)

target_link_libraries(wiremic_audio PUBLIC
    Qt6::Core
    Qt6::Network
    OpenSSL::Crypto
    PkgConfig::OPUS
)

target_compile_features(wiremic_audio PUBLIC cxx_std_20)

if(WIREMIC_BUILD_TESTS)
    add_executable(wiremic_audio_packet_codec_test src/audio/tests/audio_packet_codec_test.cpp)
    target_link_libraries(wiremic_audio_packet_codec_test PRIVATE wiremic_audio wiremic_testing)
    add_test(NAME audio_packet_codec COMMAND wiremic_audio_packet_codec_test)

    add_executable(wiremic_opus_codec_test src/audio/tests/opus_codec_test.cpp)
    target_link_libraries(wiremic_opus_codec_test PRIVATE wiremic_audio wiremic_testing)
    add_test(NAME opus_codec COMMAND wiremic_opus_codec_test)

    add_executable(wiremic_jitter_buffer_test src/audio/tests/jitter_buffer_test.cpp)
    target_link_libraries(wiremic_jitter_buffer_test PRIVATE wiremic_audio wiremic_testing)
    add_test(NAME jitter_buffer COMMAND wiremic_jitter_buffer_test)

    add_executable(wiremic_audio_transport_test src/audio/tests/audio_transport_loopback_test.cpp)
    target_link_libraries(wiremic_audio_transport_test PRIVATE wiremic_audio wiremic_testing)
    add_test(NAME audio_transport_loopback COMMAND wiremic_audio_transport_test)
endif()

add_library(wiremic_core STATIC
    src/core/ConnectionManager.cpp
)

target_include_directories(wiremic_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core
)

target_link_libraries(wiremic_core PUBLIC
    Qt6::Core
    Qt6::Network
    wiremic_protocol
    wiremic_security
    wiremic_network
    OpenSSL::Crypto
)

target_compile_features(wiremic_core PUBLIC cxx_std_20)

if(WIREMIC_BUILD_TESTS)
    add_executable(wiremic_connection_manager_test src/core/tests/connection_manager_test.cpp)
    target_link_libraries(wiremic_connection_manager_test PRIVATE wiremic_core wiremic_testing)
    add_test(NAME connection_manager COMMAND wiremic_connection_manager_test)
endif()

find_package(Qt6 REQUIRED COMPONENTS Widgets Svg)
qt_standard_project_setup()

qt_add_executable(wiremic
    src/main.cpp
    src/ui/AppController.cpp
    src/ui/MainWindow.cpp
    src/ui/IconLoader.cpp
    src/ui/components/GlassPanel.cpp
    src/ui/components/GlassButton.cpp
    src/ui/components/ToggleSwitch.cpp
    src/ui/components/NavRailButton.cpp
    src/ui/components/MicBadge.cpp
    src/ui/components/DeviceRow.cpp
    src/ui/components/StatCard.cpp
    src/ui/components/IncomingRequestDialog.cpp
    src/ui/pages/DashboardPage.cpp
    src/ui/pages/DevicesPage.cpp
    src/ui/pages/ConnectedDevicePage.cpp
    src/ui/pages/SettingsPage.cpp
    src/ui/pages/LogsPage.cpp
    src/ui/pages/AboutPage.cpp
)

qt_add_resources(wiremic "wiremic_icons"
    PREFIX "/WireMic"
    FILES
        resources/icons/app_icon.svg
        resources/icons/icon_dashboard.svg
        resources/icons/icon_devices.svg
        resources/icons/icon_connected.svg
        resources/icons/icon_settings.svg
        resources/icons/icon_logs.svg
        resources/icons/icon_about.svg
        resources/icons/icon_microphone.svg
        resources/icons/icon_check.svg
        resources/icons/icon_close.svg
        resources/icons/icon_refresh.svg
        resources/icons/icon_phone.svg
        resources/icons/icon_desktop.svg
        resources/icons/icon_wifi.svg
        resources/icons/icon_trash.svg
)

target_include_directories(wiremic PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ui
)

target_link_libraries(wiremic PRIVATE
    Qt6::Widgets
    Qt6::Svg
    wiremic_core
    wiremic_audio
    wiremic_platform
    wiremic_protocol
    wiremic_security
    wiremic_network
)

target_compile_features(wiremic PUBLIC cxx_std_20)

install(TARGETS wiremic RUNTIME DESTINATION bin)
install(FILES resources/icons/app_icon.svg DESTINATION share/icons/hicolor/scalable/apps RENAME wiremic.svg)

if(WIREMIC_BUILD_TESTS)
    find_package(Qt6 REQUIRED COMPONENTS Test)

    add_executable(wiremic_widget_interaction_test
        src/ui/tests/widget_interaction_test.cpp
        src/ui/IconLoader.cpp
        src/ui/components/GlassButton.cpp
        src/ui/components/NavRailButton.cpp
        src/ui/components/ToggleSwitch.cpp
    )
    qt_add_resources(wiremic_widget_interaction_test "wiremic_icons_test1"
        PREFIX "/WireMic"
        FILES
            resources/icons/icon_dashboard.svg
    )
    target_include_directories(wiremic_widget_interaction_test PRIVATE src/ui)
    target_link_libraries(wiremic_widget_interaction_test PRIVATE
        Qt6::Widgets Qt6::Svg Qt6::Test wiremic_testing)
    add_test(NAME widget_interaction COMMAND wiremic_widget_interaction_test -platform offscreen)

    add_executable(wiremic_icon_distinctness_test
        src/ui/tests/icon_distinctness_test.cpp
        src/ui/IconLoader.cpp
    )
    qt_add_resources(wiremic_icon_distinctness_test "wiremic_icons_test2"
        PREFIX "/WireMic"
        FILES
            resources/icons/icon_dashboard.svg
            resources/icons/icon_devices.svg
            resources/icons/icon_connected.svg
            resources/icons/icon_settings.svg
            resources/icons/icon_logs.svg
            resources/icons/icon_about.svg
            resources/icons/icon_microphone.svg
            resources/icons/icon_phone.svg
            resources/icons/icon_desktop.svg
    )
    target_include_directories(wiremic_icon_distinctness_test PRIVATE src/ui)
    target_link_libraries(wiremic_icon_distinctness_test PRIVATE
        Qt6::Widgets Qt6::Svg Qt6::Test wiremic_testing)
    add_test(NAME icon_distinctness COMMAND wiremic_icon_distinctness_test -platform offscreen)
endif()
