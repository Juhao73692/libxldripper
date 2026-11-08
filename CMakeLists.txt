cmake_minimum_required(VERSION 3.20)
if(NOT DEFINED CMAKE_OSX_DEPLOYMENT_TARGET)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")
endif()
project(libxldripper VERSION 0.0.1 LANGUAGES C OBJC)
set(xldripper_abi_version 0.0.1)

if(NOT APPLE OR NOT CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    message(FATAL_ERROR "libxldripper requires macOS.")
endif()

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

option(LIBXLDRIPPER_BUILD_CLI "Build the optional xld-rip command-line tool" OFF)
option(LIBXLDRIPPER_BUILD_STATIC "Build the static library" ON)

# Use the consumer's macOS SDK, including when linking the installed archive.
add_library(xldripper_frameworks INTERFACE)
set_target_properties(xldripper_frameworks PROPERTIES EXPORT_NAME frameworks)
target_link_libraries(xldripper_frameworks INTERFACE
    "-framework Foundation" "-framework IOKit" "-framework CoreFoundation")

add_library(xldripper_objects OBJECT
    src/device.c
    src/session.m
    src/accuraterip_id.c
    src/upstream/XLDCDDABackend.c
    src/upstream/XLDCDDARipper.m
    src/upstream/XLDSecureRipperEngine.m
    src/upstream/XLDAccurateRipDB.m
    src/upstream/XLDTrackValidator.m)
set_target_properties(xldripper_objects PROPERTIES
    C_STANDARD 11 C_STANDARD_REQUIRED YES OBJC_STANDARD 11 OBJC_STANDARD_REQUIRED YES
    POSITION_INDEPENDENT_CODE YES C_VISIBILITY_PRESET hidden OBJC_VISIBILITY_PRESET hidden)
target_include_directories(xldripper_objects PRIVATE include)
target_compile_definitions(xldripper_objects PRIVATE XLD_RIPPER_VERSION="${PROJECT_VERSION}")
target_compile_options(xldripper_objects PRIVATE -Wall -Wextra "$<$<COMPILE_LANGUAGE:OBJC>:-fno-objc-arc>")
# XLD uses CommonCrypto MD5 as an internal sector identity, independent of any
# externally visible checksum. This API is deprecated for security applications.
set_source_files_properties(src/upstream/XLDSecureRipperEngine.m PROPERTIES
    COMPILE_OPTIONS -Wno-deprecated-declarations)

function(add_xldripper_target target kind export_name)
    add_library(${target} ${kind} $<TARGET_OBJECTS:xldripper_objects>)
    add_library(libxldripper::${export_name} ALIAS ${target})
    set_target_properties(${target} PROPERTIES OUTPUT_NAME xldripper EXPORT_NAME ${export_name})
    target_include_directories(${target} PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
    target_link_libraries(${target} PRIVATE xldripper_frameworks)
endfunction()

add_xldripper_target(xldripper_shared SHARED ripper)
set_target_properties(xldripper_shared PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION ${xldripper_abi_version}
    MACOSX_RPATH YES
    LINK_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/cmake/ripper.exports")
target_link_options(xldripper_shared PRIVATE
    "LINKER:-exported_symbols_list,${CMAKE_CURRENT_SOURCE_DIR}/cmake/ripper.exports")

set(install_targets xldripper_shared xldripper_frameworks)
if(LIBXLDRIPPER_BUILD_STATIC)
    add_xldripper_target(xldripper_static STATIC ripper-static)
    list(APPEND install_targets xldripper_static)
endif()

if(LIBXLDRIPPER_BUILD_CLI)
    add_executable(xld-rip tools/xld-rip.c)
    set_target_properties(xld-rip PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED YES)
    target_compile_options(xld-rip PRIVATE -Wall -Wextra)
    target_link_libraries(xld-rip PRIVATE libxldripper::ripper)
    file(RELATIVE_PATH lib_from_bin "${CMAKE_INSTALL_FULL_BINDIR}" "${CMAKE_INSTALL_FULL_LIBDIR}")
    set_target_properties(xld-rip PROPERTIES INSTALL_RPATH "@loader_path/${lib_from_bin}")
    list(APPEND install_targets xld-rip)
endif()

install(TARGETS ${install_targets} EXPORT libxldripperTargets
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
    LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}")
install(FILES include/xld/ripper.h DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/xld")
install(FILES README.md LICENSE DESTINATION "${CMAKE_INSTALL_DATADIR}/libxldripper")
set(package_dir "${CMAKE_INSTALL_LIBDIR}/cmake/libxldripper")
configure_package_config_file(cmake/libxldripperConfig.cmake.in libxldripperConfig.cmake
    INSTALL_DESTINATION "${package_dir}")
write_basic_package_version_file(libxldripperConfigVersion.cmake VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMinorVersion)
install(EXPORT libxldripperTargets NAMESPACE libxldripper:: DESTINATION "${package_dir}")
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/libxldripperConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/libxldripperConfigVersion.cmake" DESTINATION "${package_dir}")
