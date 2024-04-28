cmake_minimum_required(VERSION 3.11)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "MinSizeRel" CACHE STRING "Choose the type of build, options are: Debug, Release, or MinSizeRel." FORCE)
    message(STATUS "No build type specified, defaulting to MinSizeRel.")
endif()

set(CMAKE_MODULE_PATH "${CMAKE_MODULE_PATH}" "${CMAKE_SOURCE_DIR}/cmake")

include (get_version)

message(STATUS "Generator ......... ${CMAKE_GENERATOR}")
message(STATUS "Build Type ........ ${CMAKE_BUILD_TYPE}")
message(STATUS "Version ........... ${PTHREADS4W_VERSION}")

project(pthreads4w VERSION ${PTHREADS4W_VERSION} LANGUAGES C)

set(PTW32_VER ${PROJECT_VERSION_MAJOR}${EXTRAVERSION})
set(CMAKE_DEBUG_POSTFIX d)

# Uncomment this if config.h defines RETAIN_WSALASTERROR
#set(XLIBS wsock32.lib)


include_directories(.)

#################################
# Target Arch                   #
#################################
include (target_arch)

get_target_arch(TARGET_ARCH)

if(${TARGET_ARCH} STREQUAL "ARM")
    add_definitions(-D__PTW32_ARCHARM -D_ARM_WINAPI_PARTITION_DESKTOP_SDK_AVAILABLE=1)
elseif(${TARGET_ARCH} STREQUAL "ARM64")
    add_definitions(-D__PTW32_ARCHARM64 -D_ARM_WINAPI_PARTITION_DESKTOP_SDK_AVAILABLE=1)
elseif(${TARGET_ARCH} STREQUAL "x86_64")
    add_definitions(-D__PTW32_ARCHAMD64)
elseif(${TARGET_ARCH} STREQUAL "x86")
    add_definitions(-D__PTW32_ARCHX86)
elseif(${TARGET_ARCH} STREQUAL "x64")
    add_definitions(-D__PTW32_ARCHX64)
else()
    MESSAGE(ERROR "\"${TARGET_ARCH}\" not supported in version.rc")
endif()
message(STATUS "Target ............ ${TARGET_ARCH}")

if(MSVC)
    message(STATUS "MSVC Version ...... ${MSVC_VERSION}")
endif()

#################################
# Install Path                  #
#################################
if(DIST_ROOT)
    set(CMAKE_INSTALL_PREFIX "${DIST_ROOT}")
else()
    set(CMAKE_INSTALL_PREFIX "${CMAKE_SOURCE_DIR}/PTHREADS-BUILT")
endif()
message(STATUS "Install Path ${CMAKE_INSTALL_PREFIX}")


set(DLLDEST ${CMAKE_INSTALL_PREFIX}/${TARGET_ARCH}/${CMAKE_BUILD_TYPE}/bin)
set(LIBDEST ${CMAKE_INSTALL_PREFIX}/${TARGET_ARCH}/${CMAKE_BUILD_TYPE}/lib)
set(HDRDEST ${CMAKE_INSTALL_PREFIX}/${TARGET_ARCH}/${CMAKE_BUILD_TYPE}/include)
set(TESTDEST ${CMAKE_INSTALL_PREFIX}/${TARGET_ARCH}/${CMAKE_BUILD_TYPE}/test)

#################################
# Defs                          #
#################################
add_definitions(-D__PTW32_BUILD_INLINED)

if(MSVC)

    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} /errorReport:none /nologo ")

    # C++ Exceptions
    # (Note: If you are using Microsoft VC++6.0, the library needs to be built
    # with /EHa instead of /EHs or else cancellation won't work properly.)
    if(MSVC_VERSION EQUAL 1200)
        set(VCEFLAGS  "/EHa /TP ")
    else()
        set(VCEFLAGS  "/EHs /TP ")
    endif()

    add_definitions(-DHAVE_CONFIG_H -D__PTW32_RC_MSC)

endif()

# Update filename with proper version info
configure_file(${CMAKE_SOURCE_DIR}/cmake/version.rc.in ${CMAKE_BINARY_DIR}/version.rc @ONLY)

#################################
# Libraries                     #
#################################
set(targ_suffix "")
if(${CMAKE_BUILD_TYPE} STREQUAL "Debug")
    set(targ_suffix ${CMAKE_DEBUG_POSTFIX})
endif()

macro(shared_lib type def)
    set(targ pthread${type}${PTW32_VER})
    add_library(${targ} SHARED pthread.c ${CMAKE_BINARY_DIR}/version.rc)
    message(STATUS ${targ})
    target_compile_definitions(${targ} PUBLIC "-D${def}")
    if(${type} STREQUAL "VCE")
        set_target_properties(${targ} PROPERTIES COMPILE_FLAGS ${VCEFLAGS})
    endif()
    if(${CMAKE_GENERATOR} MATCHES "Visual Studio")
        install(FILES ${CMAKE_BINARY_DIR}/${CMAKE_BUILD_TYPE}/${targ}${targ_suffix}.dll DESTINATION ${DLLDEST})
        install(FILES ${CMAKE_BINARY_DIR}/${CMAKE_BUILD_TYPE}/${targ}${targ_suffix}.lib DESTINATION ${LIBDEST})
    else()
        install(FILES ${CMAKE_BINARY_DIR}/${targ}${targ_suffix}.dll DESTINATION ${DLLDEST})
        install(FILES ${CMAKE_BINARY_DIR}/${targ}${targ_suffix}.lib DESTINATION ${LIBDEST})
    endif()
endmacro()

macro(static_lib type def)
    set(targ libpthread${type}${PTW32_VER})
    add_library(${targ} STATIC pthread.c)
    message(STATUS ${targ})
    target_compile_definitions(${targ} PUBLIC "-D${def}" -D__PTW32_STATIC_LIB)
    if(${type} STREQUAL "VCE")
        set_target_properties(${targ} PROPERTIES COMPILE_FLAGS ${VCEFLAGS})
    endif()
    if(${CMAKE_GENERATOR} MATCHES "Visual Studio")
        install(FILES ${CMAKE_BINARY_DIR}/${CMAKE_BUILD_TYPE}/${targ}${targ_suffix}.lib DESTINATION ${LIBDEST})
    else()
        install(FILES ${CMAKE_BINARY_DIR}/${targ}${targ_suffix}.lib DESTINATION ${LIBDEST})
    endif()
endmacro()

shared_lib ( VCE __PTW32_CLEANUP_CXX )
shared_lib ( VSE __PTW32_CLEANUP_SEH )
shared_lib ( VC  __PTW32_CLEANUP_C )

static_lib ( VCE __PTW32_CLEANUP_CXX )
static_lib ( VSE __PTW32_CLEANUP_SEH )
static_lib ( VC  __PTW32_CLEANUP_C )

#################################
# Install                       #
#################################
install(FILES _ptw32.h pthread.h sched.h semaphore.h DESTINATION ${HDRDEST})

#################################
# Test                          #
#################################
option(ENABLE_TESTS "Enable Test code build" FALSE)

#TODO determine if cross compile...
if(ENABLE_TESTS)
    add_subdirectory(tests)
endif()
