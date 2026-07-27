add_library(wiremic_protocol STATIC
    protocol/Protocol.cpp
    protocol/MessageFraming.cpp
)

target_include_directories(wiremic_protocol PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/protocol
)

find_package(nlohmann_json 3.11 REQUIRED)
target_link_libraries(wiremic_protocol PUBLIC nlohmann_json::nlohmann_json)

target_compile_features(wiremic_protocol PUBLIC cxx_std_20)

add_library(wiremic_testing INTERFACE)
target_include_directories(wiremic_testing INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/testing
)

if(WIREMIC_BUILD_TESTS)
    add_executable(wiremic_protocol_test protocol/tests/protocol_test.cpp)
    target_link_libraries(wiremic_protocol_test PRIVATE wiremic_protocol wiremic_testing)
    add_test(NAME protocol_roundtrip COMMAND wiremic_protocol_test)

    add_executable(wiremic_framing_test protocol/tests/message_framing_test.cpp)
    target_link_libraries(wiremic_framing_test PRIVATE wiremic_protocol wiremic_testing)
    add_test(NAME message_framing COMMAND wiremic_framing_test)
endif()