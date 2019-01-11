#Copyright (c) Microsoft. All rights reserved.
#Licensed under the MIT license. See LICENSE file in the project root for full license information.

cmake_minimum_required(VERSION 2.8.11)
#this is CMakeLists for pnp_client

if(POLICY CMP0042)
    cmake_policy(SET CMP0042 NEW)
endif()

compileAsC99()

set(pnp_client_c_files
    ./src/lock_thread_binding_impl.c
    ./src/lock_thread_binding_stub.c
    ./src/pnp_client_core.c
    ./src/pnp_device.c
    ./src/pnp_device_ll.c
    ./src/pnp_interface.c
    ./src/pnp_interface_core.c
    ./src/pnp_interface_list.c
    ./src/pnp_interface_ll.c
    ./src/pnp_raw_interface.c
    ./src/pnp_version.c
)

set(pnp_client_h_files
    ./inc/pnp_client_version.h
    ./inc/pnp_device_client.h
    ./inc/pnp_interface_client.h
    ./inc/pnp_interface_client_ll.h
    ./inc/internal/lock_thread_binding.h
    ./inc/internal/lock_thread_binding_impl.h
    ./inc/internal/lock_thread_binding_stub.h
    ./inc/internal/pnp_client_core.h
    ./inc/internal/pnp_interface_core.h
    ./inc/internal/pnp_interface_list.h
    ./inc/internal/pnp_raw_interface.h
)

set(pnp_client_libs)

set(install_staticlibs
    pnp_client
)

set(pnp_client_h_install_files
    ${pnp_client_h_files}
)

set(PNP_CLIENT_INC_FOLDER ${CMAKE_CURRENT_LIST_DIR}/inc ${CMAKE_CURRENT_LIST_DIR}/inc/internal CACHE INTERNAL "this is what needs to be included if using pnp_client lib" FORCE)

include_directories(../deps/parson)
include_directories(${AZURE_C_SHARED_UTILITY_INCLUDES})
include_directories(${SHARED_UTIL_INC_FOLDER})
include_directories(${IOTHUB_CLIENT_INC_FOLDER})
include_directories(${PNP_CLIENT_INC_FOLDER})

if(${memory_trace})
    add_definitions(-DGB_MEASURE_MEMORY_FOR_THIS -DGB_DEBUG_ALLOC)
endif()


IF(WIN32)
    #windows needs this define
    add_definitions(-D_CRT_SECURE_NO_WARNINGS)
    add_definitions(-DGB_MEASURE_MEMORY_FOR_THIS -DGB_DEBUG_ALLOC)
ENDIF(WIN32)


add_library(pnp_client
    ${pnp_client_c_files}
    ${pnp_client_h_files}
)
setSdkTargetBuildProperties(pnp_client)
target_link_libraries(pnp_client ${pnp_client_libs})
target_link_libraries(pnp_client parson)

linkSharedUtil(pnp_client)
set(pnp_client_libs
    pnp_client
    ${pnp_client_libs}
)
if(NOT IN_OPENWRT)
    # Disable tests for OpenWRT
    add_subdirectory(tests)
endif()

if(NOT ${skip_samples})
    add_subdirectory(samples)
    add_subdirectory(samples_ll)
endif()

if (${build_as_dynamic})
    set(pnp_def_file ./src/pnp_client_dll.def)
    
    add_library(pnp_client_dll SHARED
        ${pnp_client_c_files}
         ${pnp_client_h_files}
        ${pnp_def_file}
    )
    
    setSdkTargetBuildProperties(pnp_client_dll)
    target_link_libraries(pnp_client_dll ${pnp_client_libs} iothub_client_dll)
    if(NOT WIN32)
        set_target_properties(pnp_client_dll PROPERTIES OUTPUT_NAME "pnp_client")
    endif()
    
    set_target_properties(pnp_client_dll PROPERTIES
        ARCHIVE_OUTPUT_NAME "pnp_client_dll_import"
        ENABLE_EXPORTS YES
        VERSION ${IOT_SDK_VERSION}
        SOVERSION ${IOT_SDK_VERION_MAJOR}
        BUILD_WITH_INSTALL_RPATH TRUE
    )
    
    target_link_libraries(pnp_client_dll parson)
    
    if (${CMAKE_C_COMPILER_ID} STREQUAL "GNU" OR ${CMAKE_C_COMPILER_ID} STREQUAL "Clang")
        target_link_libraries(pnp_client_dll
            "-Wl,--exclude-libs,libparson.a"
        )
    endif()
    
    set(install_staticlibs ${install_staticlibs} pnp_client_dll)
endif()

install(FILES ${pnp_client_h_install_files}
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/azureiot/pnp)
install(TARGETS ${install_staticlibs}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR})
