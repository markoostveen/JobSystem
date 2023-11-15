cmake_minimum_required(VERSION 3.20)

include_guard(GLOBAL)

# Call this macro to download and setup VCPKG
macro(DownloadAndSetupVCPKG)

    # Set the desired version of vcpkg
    set(VCPKG_COMMIT_ID "36fb23307e10cc6ffcec566c46c4bb3f567c82c6")

    # Define the path where vcpkg will be installed
    set(ENV{VCPKG_ROOT} "${CMAKE_BINARY_DIR}/vcpkg")

    # Check if vcpkg is already installed
    if(NOT EXISTS "$ENV{VCPKG_ROOT}/bootstrap-vcpkg.sh")
        # Download vcpkg
        message(STATUS "Downloading vcpkg...")
        find_package(Git REQUIRED)
        execute_process(
            COMMAND ${GIT_EXECUTABLE} clone https://github.com/microsoft/vcpkg.git $ENV{VCPKG_ROOT}
            RESULT_VARIABLE GIT_CLONE_RESULT
        )
        if(NOT GIT_CLONE_RESULT EQUAL "0")
            message(FATAL_ERROR "git clone of vcpkg failed")
        endif()

        # Checkout the specific commit or tag
        execute_process(
            COMMAND ${GIT_EXECUTABLE} -C $ENV{VCPKG_ROOT} checkout ${VCPKG_COMMIT_ID}
            RESULT_VARIABLE GIT_CHECKOUT_RESULT
        )
        if(NOT GIT_CHECKOUT_RESULT EQUAL "0")
            message(FATAL_ERROR "git checkout of vcpkg failed")
        endif()
    endif()

    # Bootstrap vcpkg
    message(STATUS "Bootstrapping vcpkg...")
    if(WIN32)
        execute_process(
            COMMAND $ENV{VCPKG_ROOT}/bootstrap-vcpkg.bat
            RESULT_VARIABLE BOOTSTRAP_RESULT
        )
    elseif(UNIX AND NOT APPLE)
        execute_process(
            COMMAND $ENV{VCPKG_ROOT}/bootstrap-vcpkg.sh
            RESULT_VARIABLE BOOTSTRAP_RESULT
        )
    else()
        message(ERROR "This setup script doesn't support your platform yet")
    endif()

    if(NOT BOOTSTRAP_RESULT EQUAL "0")
        message(FATAL_ERROR "Bootstrap of vcpkg failed")
    endif()

endmacro()