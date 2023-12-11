cmake_minimum_required(VERSION 3.20)

# QT specific
# set(CMAKE_AUTOUIC ON)
# set(CMAKE_AUTOMOC ON)
# set(CMAKE_AUTORCC ON)

# prevent multiple inclusions of macroFunctions
include_guard(GLOBAL)

option(FORCE_Portable_VCPKG "(Requires reconfigure) Force download of a portable VCPKG repository to avoid conflicts" ON)

if(${FORCE_Portable_VCPKG})
    # undefine vcpkg path to force VCPKG download
    set(ENV{VCPKG_ROOT} "")
    DownloadAndSetupVCPKG()
endif()

# Configure vcpkg toolchain
if("${CMAKE_TOOLCHAIN_FILE}" STREQUAL "")
    if(NOT DEFINED ENV{VCPKG_ROOT})
        set(FORCE_Portable_VCPKG TRUE)
        DownloadAndSetupVCPKG()
    endif()
    set(CMAKE_TOOLCHAIN_FILE "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake" CACHE STRING "")
endif()

set(VCPKG_OVERLAY_TRIPLETS "${CMAKE_BINARY_DIR}/vcpkg_custom")

macro(AlterVCPKGTriplet tripletName)

    if(NOT DEFINED ENV{VCPKG_ROOT})
        message(ERROR " VCPKG_ROOT was not found, please define it before altering a triplet")
    endif()

    # Set the path to the vcpkg triplets directory
    set(VCPKG_TRIPLETS_DIR "$ENV{VCPKG_ROOT}/triplets")

    set(TRIPLET_BASEFILE_NAME "${tripletName}.cmake")
    set(TRIPLET_FILE_NAME "${tripletName}-custom.cmake")


    # Specify the source and destination triplet files
    set(SOURCE_TRIPLET_FILE "${VCPKG_TRIPLETS_DIR}/${TRIPLET_BASEFILE_NAME}")
    set(DESTINATION_TRIPLET_FILE "${VCPKG_OVERLAY_TRIPLETS}/${TRIPLET_FILE_NAME}")

    # Copy the source triplet file to the destination
    file(COPY ${SOURCE_TRIPLET_FILE} DESTINATION ${VCPKG_OVERLAY_TRIPLETS})

    set(DESTINATION_TEMPLATE_TRIPLE_FILE "${VCPKG_OVERLAY_TRIPLETS}/${TRIPLET_FILE_NAME}.in")
    file(RENAME "${VCPKG_OVERLAY_TRIPLETS}/${TRIPLET_BASEFILE_NAME}" "${DESTINATION_TEMPLATE_TRIPLE_FILE}")

    
    # Read the content of the custom triplet template
    set(CUSTOM_TRIPLET_FILE "${DESTINATION_TRIPLET_FILE}.in")
    file(READ "${CUSTOM_TRIPLET_FILE}" CUSTOM_TRIPLET_CONTENT)

    # Read the content of the source triplet file
    file(WRITE ${DESTINATION_TEMPLATE_TRIPLE_FILE} "\n${CUSTOM_TRIPLET_CONTENT}")

    # Set variables to be configured
    set(VCPKG_CMAKE_CONFIGURE_OPTIONS "")

    # Use configure_file to append custom configurations
    configure_file("${DESTINATION_TEMPLATE_TRIPLE_FILE}" "${DESTINATION_TRIPLET_FILE}" @ONLY)
endmacro()

macro(RunVCPKG)
    option(ERS_USE_DEFAULT_VCPKG_INSTALL_DIR "Prevent reinstalling of packages of vcpkg after deleting build directory" OFF)

    if("${CMAKE_TOOLCHAIN_FILE}" STREQUAL "")
        message(ERROR " unable to find vcpkg")
    endif()

    if(ERS_USE_DEFAULT_VCPKG_INSTALL_DIR)
        if(DEFINED ENV{VCPKG_ROOT} AND NOT DEFINED VCPKG_INSTALLED_DIR)
            set(VCPKG_INSTALLED_DIR "$ENV{VCPKG_ROOT}/installed")
        endif()
    endif()

    if(WIN32)
        AlterVCPKGTriplet(x64-windows)
        set(VCPKG_TARGET_TRIPLET "x64-windows-custom" CACHE STRING "")
    elseif(UNIX AND NOT APPLE)
        AlterVCPKGTriplet(x64-linux)
        set(VCPKG_TARGET_TRIPLET "x64-linux-custom" CACHE STRING "")
    else()
        message(ERROR " Please change VCPKG_TARGET_TRIPLET to correct vcpkg triplet to use")
    endif()

    include(${CMAKE_TOOLCHAIN_FILE})
endmacro()