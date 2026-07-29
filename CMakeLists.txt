cmake_minimum_required(VERSION 3.24)
project(wiremic LANGUAGES CXX)

set(WIREMIC_VERSION "1.0.0")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_INCLUDE_CURRENT_DIR ON)

option(WIREMIC_BUILD_TESTS "Build unit tests" ON)

if(WIREMIC_BUILD_TESTS)
    enable_testing()
endif()

add_subdirectory(shared)
add_subdirectory(linux-app)

set(CPACK_PACKAGE_NAME "wiremic")
set(CPACK_PACKAGE_VENDOR "Arvinkheradmand")
set(CPACK_PACKAGE_CONTACT "Arvinkheradmand")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY
    "Use your phone as a microphone over the local network")
set(CPACK_PACKAGE_HOMEPAGE_URL
    "https://github.com/adfchcjnkb/wiremic-linux-app")
set(CPACK_PACKAGE_VERSION "${WIREMIC_VERSION}")
set(CPACK_PACKAGING_INSTALL_PREFIX "/usr")
set(CPACK_STRIP_FILES ON)

set(CPACK_DEBIAN_PACKAGE_SECTION "sound")
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
set(CPACK_DEBIAN_PACKAGE_DEPENDS
    "libqt6widgets6, libqt6network6, libqt6svg6, libopus0, libssl3, libpipewire-0.3-0, libpulse0, pulseaudio-utils")

set(CPACK_RPM_PACKAGE_GROUP "Applications/Multimedia")
set(CPACK_RPM_PACKAGE_LICENSE "MIT")
set(CPACK_RPM_PACKAGE_REQUIRES
    "qt6-qtbase, qt6-qtsvg, opus, openssl-libs, pipewire-libs, pulseaudio-libs, pulseaudio-utils")
set(CPACK_RPM_EXCLUDE_FROM_AUTO_FILELIST_ADDITION
    "/usr/share/icons/hicolor" "/usr/share/icons" "/usr/share/applications")

include(CPack)
