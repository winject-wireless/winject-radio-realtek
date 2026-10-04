include(${CMAKE_CURRENT_SOURCE_DIR}/../BfcFetch.cmake)

include(FetchContent)
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
FetchContent_Declare(googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.14.0
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(googletest)

set(VENDOR_MPLANE ${CMAKE_CURRENT_SOURCE_DIR}/../vendor/mplane)

add_executable(winject-radio-realtek-tests
    ConfigTest.cpp
    DplaneClassifyTest.cpp
    MplaneVersionTest.cpp
    DeviceSelectorTest.cpp
    DeviceWatchTest.cpp
    ModulationTest.cpp
    RadiotapTest.cpp
    PowerCalTest.cpp
    RxFilterBpfTest.cpp
    FcsTest.cpp
    SettingsTest.cpp
    ../radio/Config.cpp
    ../radio/Log.cpp
    ../radio/DeviceSelector.cpp
    ../radio/DeviceWatch.cpp
    ../radio/Modulation.cpp
    ../radio/Radiotap.cpp
    ../radio/PowerCal.cpp
    ../radio/RxFilterBpf.cpp
    ../radio/Fcs.cpp
    ../radio/Settings.cpp
    ../radio/DplaneClassify.cpp
    mplane_args_test.cpp
    mplane_commands_test.cpp
    ${VENDOR_MPLANE}/config_types.cpp
    ${VENDOR_MPLANE}/mplane_args.cpp
    ${VENDOR_MPLANE}/mplane_commands.cpp
    ${VENDOR_MPLANE}/mplane_reply.cpp
    ${VENDOR_MPLANE}/mplane_req_id.cpp
    ${VENDOR_MPLANE}/mplane_version.cpp
)
target_include_directories(winject-radio-realtek-tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../radio
    ${VENDOR_MPLANE}
    ${CMAKE_BINARY_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../vendor/radiotap
    ${CMAKE_CURRENT_SOURCE_DIR}/../..
    ${bfc_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)
target_link_libraries(winject-radio-realtek-tests PRIVATE
    winject-radiotap
    GTest::gtest_main
    pthread
)
target_compile_definitions(winject-radio-realtek-tests PRIVATE
    WINJECT_RADIO_TEST
    SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)
include(GoogleTest)
gtest_discover_tests(winject-radio-realtek-tests)
