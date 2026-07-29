find_package(Qt6 REQUIRED COMPONENTS Core Network Widgets Svg)
qt_standard_project_setup()

find_package(OpenSSL REQUIRED)

find_package(nlohmann_json 3.11 REQUIRED)

if(WIN32)
    find_package(Opus CONFIG REQUIRED)
    set(WIREMIC_OPUS_TARGET Opus::opus)
else()
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(OPUS REQUIRED IMPORTED_TARGET opus)
    set(WIREMIC_OPUS_TARGET PkgConfig::OPUS)
endif()

if(WIN32)
    set(WIREMIC_PLATFORM_SOURCES src/platform/WindowsVirtualMic.cpp)
    set(WIREMIC_PLATFORM_LIBS ole32 oleaut32 winmm ws2_32 iphlpapi shell32 uuid)
    set(WIREMIC_PLATFORM_INCLUDES "")
else()
    pkg_check_modules(PIPEWIRE REQUIRED IMPORTED_TARGET libpipewire-0.3)
    pkg_check_modules(SPA REQUIRED IMPORTED_TARGET libspa-0.2)
    pkg_check_modules(PULSE REQUIRED IMPORTED_TARGET libpulse-simple libpulse)
    set(WIREMIC_PLATFORM_SOURCES
        src/platform/PipeWireVirtualMic.cpp
        src/platform/PipeWireAudioCapture.cpp
        src/platform/AudioDeviceLister.cpp
        src/platform/PulseAudioVirtualMic.cpp)
    set(WIREMIC_PLATFORM_LIBS PkgConfig::PIPEWIRE PkgConfig::SPA PkgConfig::PULSE)
    set(WIREMIC_PLATFORM_INCLUDES ${PIPEWIRE_INCLUDE_DIRS} ${SPA_INCLUDE_DIRS})
endif()

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

add_library(wiremic_network STATIC
    src/network/DiscoveryService.cpp
    src/network/ControlServer.cpp
    src/network/ControlClient.cpp
)

target_include_directories(wiremic_network PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src/network
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core/security
    ${CMAKE_CURRENT_SOURCE_DIR}/../shared/platform
)

target_link_libraries(wiremic_network PUBLIC
    Qt6::Core
    Qt6::Network
    wiremic_protocol
    wiremic_security
)

target_compile_features(wiremic_network PUBLIC cxx_std_20)

add_library(wiremic_platform STATIC
    ${WIREMIC_PLATFORM_SOURCES}
    src/platform/VirtualMicBackend.cpp
)

target_include_directories(wiremic_platform PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src/platform
    ${WIREMIC_PLATFORM_INCLUDES}
)

target_link_libraries(wiremic_platform PUBLIC
    Qt6::Core
    ${WIREMIC_PLATFORM_LIBS}
)

target_compile_features(wiremic_platform PUBLIC cxx_std_20)

add_library(wiremic_audio STATIC
    src/audio/AudioPacketCodec.cpp
    src/audio/OpusCodec.cpp
    src/audio/JitterBuffer.cpp
    src/audio/AudioSender.cpp
    src/audio/AudioReceiver.cpp
    src/audio/UdpSocketBinder.cpp
)

target_include_directories(wiremic_audio PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src/audio
    ${CMAKE_CURRENT_SOURCE_DIR}/../shared/platform
)

target_link_libraries(wiremic_audio PUBLIC
    Qt6::Core
    Qt6::Network
    OpenSSL::Crypto
    ${WIREMIC_OPUS_TARGET}
)

target_compile_features(wiremic_audio PUBLIC cxx_std_20)

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
    wiremic_audio
    wiremic_platform
    OpenSSL::Crypto
)

target_compile_features(wiremic_core PUBLIC cxx_std_20)

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
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ui/components
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ui/pages
    ${WIREMIC_PLATFORM_INCLUDES}
)

target_link_libraries(wiremic PRIVATE
    Qt6::Widgets
    Qt6::Svg
    Qt6::Core
    Qt6::Network
    wiremic_core
    wiremic_audio
    wiremic_platform
    wiremic_protocol
    wiremic_security
    wiremic_network
)

if(WIN32)
    set_target_properties(wiremic PROPERTIES WIN32_EXECUTABLE TRUE)
    target_sources(wiremic PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../packaging/windows/wiremic.rc)
endif()

target_compile_features(wiremic PUBLIC cxx_std_20)

if(WIN32)
    # Resolve every non-system DLL the executable actually pulls in, rather
    # than naming them by hand. windeployqt only knows about Qt, so opus and
    # OpenSSL would otherwise be left behind.
    #
    # The resolver does not search PATH or vcpkg on its own: it looks beside
    # the binary and in DIRECTORIES only. Without these it silently resolves
    # nothing and the package ships incomplete, which is exactly what happened.
    set(WIREMIC_DEP_DIRS "")
    if(DEFINED VCPKG_INSTALLED_DIR AND DEFINED VCPKG_TARGET_TRIPLET)
        list(APPEND WIREMIC_DEP_DIRS
             "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin")
    endif()

    # Also derive the location from the imported targets themselves, so this
    # keeps working if the dependencies ever come from somewhere other than
    # vcpkg. Import libraries live in lib/ while the DLLs sit in bin/.
    foreach(dep Opus::opus OpenSSL::Crypto OpenSSL::SSL)
        if(TARGET ${dep})
            foreach(prop IMPORTED_LOCATION_RELEASE IMPORTED_LOCATION
                         IMPORTED_IMPLIB_RELEASE IMPORTED_IMPLIB)
                get_target_property(dep_path ${dep} ${prop})
                if(dep_path)
                    get_filename_component(dep_dir "${dep_path}" DIRECTORY)
                    get_filename_component(dep_root "${dep_dir}" DIRECTORY)
                    list(APPEND WIREMIC_DEP_DIRS "${dep_dir}" "${dep_root}/bin")
                endif()
            endforeach()
        endif()
    endforeach()
    list(REMOVE_DUPLICATES WIREMIC_DEP_DIRS)
    message(STATUS "Runtime dependency search path: ${WIREMIC_DEP_DIRS}")

    install(TARGETS wiremic
            RUNTIME_DEPENDENCY_SET wiremic_runtime_deps
            RUNTIME DESTINATION .)
    install(RUNTIME_DEPENDENCY_SET wiremic_runtime_deps
            DIRECTORIES ${WIREMIC_DEP_DIRS}
            PRE_EXCLUDE_REGEXES
                "api-ms-win-.*" "ext-ms-.*" "hvsifiletrust" "pdmutilities"
            POST_EXCLUDE_REGEXES
                ".*[Ss]ystem32.*" ".*[Ww]indows[/\\][Ss]ys.*"
            DESTINATION .)
    install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/../packaging/icons/wiremic.ico
            DESTINATION .)
else()
    install(TARGETS wiremic RUNTIME DESTINATION bin)
    install(FILES resources/icons/app_icon.svg
            DESTINATION share/icons/hicolor/scalable/apps RENAME wiremic.svg)
endif()

if(UNIX AND NOT APPLE)
    install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/../packaging/linux/wiremic.desktop
            DESTINATION share/applications)
    foreach(size 16 24 32 48 64 128 256)
        install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/../packaging/icons/wiremic-${size}.png
                DESTINATION share/icons/hicolor/${size}x${size}/apps
                RENAME wiremic.png)
    endforeach()
endif()

if(WIREMIC_BUILD_TESTS)
    find_package(Qt6 REQUIRED COMPONENTS Test)

    add_executable(wiremic_widget_interaction_test
        src/ui/tests/widget_interaction_test.cpp
        src/ui/IconLoader.cpp
        src/ui/components/GlassButton.cpp
        src/ui/components/NavRailButton.cpp
        src/ui/components/ToggleSwitch.cpp
        src/ui/components/DeviceRow.cpp
        src/ui/pages/DevicesPage.cpp
    )
    qt_add_resources(wiremic_widget_interaction_test "wiremic_icons_test1"
        PREFIX "/WireMic"
        FILES
            resources/icons/icon_dashboard.svg
            resources/icons/icon_phone.svg
            resources/icons/icon_desktop.svg
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

    function(wiremic_add_test test_name source)
        set(target "wiremic_${test_name}_test")
        add_executable(${target} ${source})
        target_link_libraries(${target} PRIVATE wiremic_testing ${ARGN})
        add_test(NAME ${test_name} COMMAND ${target})
        set_tests_properties(${test_name} PROPERTIES TIMEOUT 120)
    endfunction()

    wiremic_add_test(audio_packet_codec src/audio/tests/audio_packet_codec_test.cpp
        wiremic_audio)
    wiremic_add_test(jitter_buffer src/audio/tests/jitter_buffer_test.cpp
        wiremic_audio)
    wiremic_add_test(opus_codec src/audio/tests/opus_codec_test.cpp
        wiremic_audio)
    wiremic_add_test(audio_transport_loopback
        src/audio/tests/audio_transport_loopback_test.cpp
        wiremic_audio Qt6::Core Qt6::Network)
    wiremic_add_test(audio_latency
        src/audio/tests/audio_latency_test.cpp
        wiremic_audio Qt6::Core Qt6::Network)
    wiremic_add_test(audio_fidelity
        src/audio/tests/audio_fidelity_test.cpp
        wiremic_audio)
    wiremic_add_test(packet_loss
        src/audio/tests/packet_loss_test.cpp
        wiremic_audio)

    add_executable(wiremic_mono_resampler_test
        src/audio/tests/mono_resampler_test.cpp)
    target_include_directories(wiremic_mono_resampler_test PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../android-app/app/src/main/cpp/audio)
    target_link_libraries(wiremic_mono_resampler_test PRIVATE wiremic_testing)
    add_test(NAME mono_resampler COMMAND wiremic_mono_resampler_test)

    wiremic_add_test(control_channel_loopback
        src/network/tests/control_channel_loopback_test.cpp
        wiremic_network Qt6::Core Qt6::Network)
    wiremic_add_test(discovery_smoke src/network/tests/discovery_smoke_test.cpp
        wiremic_network Qt6::Core Qt6::Network)

    wiremic_add_test(connection_manager src/core/tests/connection_manager_test.cpp
        wiremic_core Qt6::Core Qt6::Network)

    wiremic_add_test(certificate_manager
        src/core/security/tests/certificate_manager_test.cpp wiremic_security)
    wiremic_add_test(trusted_device_store
        src/core/security/tests/trusted_device_store_test.cpp wiremic_security)

    wiremic_add_test(virtual_mic src/platform/tests/virtual_mic_test.cpp
        wiremic_platform)
    if(NOT WIN32)
        wiremic_add_test(audio_capture src/platform/tests/audio_capture_test.cpp
            wiremic_platform)
    endif()
endif()
