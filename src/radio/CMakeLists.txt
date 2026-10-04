cmake_minimum_required(VERSION 3.16)

include(${CMAKE_CURRENT_SOURCE_DIR}/../BfcFetch.cmake)

set(VENDOR_MPLANE ${CMAKE_CURRENT_SOURCE_DIR}/../vendor/mplane)
set(VENDOR_RT ${CMAKE_CURRENT_SOURCE_DIR}/../vendor/radiotap)

add_library(winject-mplane STATIC
    ${VENDOR_MPLANE}/config_types.cpp
    ${VENDOR_MPLANE}/mplane_args.cpp
    ${VENDOR_MPLANE}/mplane_commands.cpp
    ${VENDOR_MPLANE}/mplane_reply.cpp
    ${VENDOR_MPLANE}/mplane_req_id.cpp
)
target_include_directories(winject-mplane PUBLIC ${VENDOR_MPLANE})
target_compile_options(winject-mplane PRIVATE -Wno-unused-parameter)

add_library(winject-radiotap STATIC
    ${VENDOR_RT}/radiotap.c
)
target_include_directories(winject-radiotap PUBLIC ${VENDOR_RT})

find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBNL REQUIRED libnl-3.0 libnl-genl-3.0)

add_executable(winject-radio-realtek
    Main.cpp
    App.cpp
    Config.cpp
    Log.cpp
    DeviceSelector.cpp
    Nl80211.cpp
    NetLink.cpp
    Modulation.cpp
    PowerCal.cpp
    Radiotap.cpp
    Fcs.cpp
    PacketSocket.cpp
    RxFilterBpf.cpp
    Injector.cpp
    Forwarder.cpp
    DataPlane.cpp
    Settings.cpp
    RealtekRadio.cpp
    RealtekBackends.cpp
    MplaneServer.cpp
    NetUtil.cpp
    DeviceWatch.cpp
)
target_include_directories(winject-radio-realtek PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${VENDOR_MPLANE}
    ${bfc_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${LIBNL_INCLUDE_DIRS}
)
target_link_libraries(winject-radio-realtek PRIVATE
    winject-mplane
    winject-radiotap
    pthread
    ${LIBNL_LIBRARIES}
)
target_compile_options(winject-radio-realtek PRIVATE ${LIBNL_CFLAGS_OTHER})
target_compile_options(winject-radio-realtek PRIVATE
    -Wall -Wextra -Werror=return-type
)
