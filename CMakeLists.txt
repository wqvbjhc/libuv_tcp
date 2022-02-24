#################### project ################################# 
cmake_minimum_required(VERSION 3.16)
if(POLICY CMP0091)
    cmake_policy(SET CMP0091 NEW)
elseif(MSVC)
    message(FATAL_ERROR "Please upgrade your CMake version to >= 3.15.")
endif()
project(libuv_tcp)
set(CMAKE_INSTALL_PREFIX "${CMAKE_BINARY_DIR}/install" CACHE PATH "Installation Directory" FORCE)

set(platform_link_flags "")
if(CMAKE_GENERATOR MATCHES "Visual Studio" OR MSVC)
    message("MSVC")
    #### 设置多核编译
    add_compile_options($<$<CXX_COMPILER_ID:MSVC>:/MP>)

    #### 设置C++17
    #set(CMAKE_CXX_STANDARD 17)
    add_compile_options($<$<CXX_COMPILER_ID:MSVC>:/std:c++latest>)

    #### 设置msvc static runtime library
    get_directory_property(hasParent PARENT_DIRECTORY)
    if(hasParent)
        set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>" PARENT_SCOPE)
    else()
        set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    endif()
    #### 解决min,max冲突
    # add_definitions(-DNOMINMAX)
    add_compile_options($<$<CXX_COMPILER_ID:MSVC>:-DNOMINMAX>)
    add_compile_options($<$<CXX_COMPILER_ID:MSVC>:-D_CRT_SECURE_NO_WARNINGS>)

    #### fatal error C1060: 编译器的堆空间不足
    add_compile_options($<$<CXX_COMPILER_ID:MSVC>:/Zm2000>)

    ### find libuv
    set(LIBUV_DIR ${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/libuv)
    if(CMAKE_CL_64 OR ${CMAKE_SIZEOF_VOID_P} EQUAL "8")        # -- 64-bit builds.
      set(LIBUV_LIB_DIRS "${LIBUV_DIR}/lib/vc_lib_x64")
    else()                                                     # -- 32-bit builds.
      set(LIBUV_LIB_DIRS "${LIBUV_DIR}/lib/vc_lib")
    endif()
    set(LIBUV_FOUND True)
    set(LIBUV_INCLUDE_DIRS ${LIBUV_DIR}/include)
    set(LIBUV_LIBRARIES libuv$<$<CONFIG:Debug>:d>) # check if debug
    include_directories(${LIBUV_INCLUDE_DIRS})
    link_directories(${LIBUV_LIB_DIRS})

    ### find openssl
    set(OpenSSL_DIR ${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/openssl)
    if(CMAKE_CL_64 OR ${CMAKE_SIZEOF_VOID_P} EQUAL "8")        # -- 64-bit builds.
      set(OPENSSL_LIB_DIRS "${OpenSSL_DIR}/lib/vc_lib_x64")
    else()                                                     # -- 32-bit builds.
      set(OPENSSL_LIB_DIRS "${OpenSSL_DIR}/lib/vc_lib")
    endif()
    set(OpenSSL_FOUND True)
    set(OPENSSL_INCLUDE_DIR ${OpenSSL_DIR}/include)
    include_directories(${OPENSSL_INCLUDE_DIR})
    link_directories(${OPENSSL_LIB_DIRS})
    set(OPENSSL_LIBRARIES
        libeay32$<$<CONFIG:Debug>:d> ssleay32$<$<CONFIG:Debug>:d>)
    
    ########
    list(APPEND platform_link_flags Iphlpapi Userenv) 

else()
    add_compile_options(-fPIC)
    ################### check c++11 or c++0x ################################### 
    include(CheckCXXCompilerFlag)
    CHECK_CXX_COMPILER_FLAG("-std=c++17" COMPILER_SUPPORTS_CXX17)
    CHECK_CXX_COMPILER_FLAG("-std=c++11" COMPILER_SUPPORTS_CXX11)
    if(COMPILER_SUPPORTS_CXX17)
    set(CMAKE_CXX_STANDARD 17)
    # set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++17")
    elseif(COMPILER_SUPPORTS_CXX11)
    set(CMAKE_CXX_STANDARD 11)
    # set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11")
    else()
        message(STATUS "The compiler ${CMAKE_CXX_COMPILER} has no C++11 support. Please use a different C++ compiler.")
    endif()

    ### find libuv
    set(LIBUV_DIR ${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/libuv)
    if(CMAKE_CL_64 OR ${CMAKE_SIZEOF_VOID_P} EQUAL "8")        # -- 64-bit builds.
      set(LIBUV_LIB_DIRS "${LIBUV_DIR}/lib/linux_lib_x64")
    else()                                                     # -- 32-bit builds.
      set(LIBUV_LIB_DIRS "${LIBUV_DIR}/lib/linux_lib")
    endif()
    set(LIBUV_FOUND True)
    set(LIBUV_INCLUDE_DIRS ${LIBUV_DIR}/include)
    set(LIBUV_LIBRARIES uv)
    include_directories(${LIBUV_INCLUDE_DIRS})
    link_directories(${LIBUV_LIB_DIRS})

    ### find openssl
    set(OpenSSL_DIR ${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/openssl)
    if(CMAKE_CL_64 OR ${CMAKE_SIZEOF_VOID_P} EQUAL "8")        # -- 64-bit builds.
      set(OPENSSL_LIB_DIRS "${OpenSSL_DIR}/lib/linux_lib_x64")
    else()                                                     # -- 32-bit builds.
      set(OPENSSL_LIB_DIRS "${OpenSSL_DIR}/lib/linux_lib")
    endif()
    set(OpenSSL_FOUND True)
    set(OPENSSL_INCLUDE_DIR ${OpenSSL_DIR}/include)
    include_directories(${OPENSSL_INCLUDE_DIR})
    link_directories(${OPENSSL_LIB_DIRS})
    set(OPENSSL_LIBRARIES crypto ssl)

    list(APPEND platform_link_flags pthread)

endif()

message(STATUS "Find Libuv ${LIBUV_FOUND}")
message(STATUS "     Libuv include dir: ${LIBUV_INCLUDE_DIRS}")
message(STATUS "     Libuv libraries: ${LIBUV_LIBRARIES}")
message(STATUS "Find OpenSSL ${OpenSSL_FOUND}")
message(STATUS "     OpenSSL include dir: ${OPENSSL_INCLUDE_DIR}")
message(STATUS "     OpenSSL libraries: ${OPENSSL_LIBRARIES}")

################################################################ 
include_directories(${CMAKE_CURRENT_SOURCE_DIR})
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/log4z)

set(test_tcpclient_reconnect 
    test_tcpclient_reconnect.cpp
    tcpclient.cpp
    log4z/log4z.cpp
)

set(test_tcpclient 
    test_tcpclient.cpp
    tcpclient.cpp
    log4z/log4z.cpp
)

set(test_tcpserver 
    test_tcpserver.cpp
    tcpserver.cpp
    log4z/log4z.cpp
)

add_executable(test_tcpclient_reconnect ${test_tcpclient_reconnect})
target_link_libraries(test_tcpclient_reconnect ${LIBUV_LIBRARIES} ${OPENSSL_LIBRARIES} ${platform_link_flags})

add_executable(test_tcpclient ${test_tcpclient})
target_link_libraries(test_tcpclient ${LIBUV_LIBRARIES} ${OPENSSL_LIBRARIES} ${platform_link_flags})

add_executable(test_tcpserver ${test_tcpserver})
target_link_libraries(test_tcpserver ${LIBUV_LIBRARIES} ${OPENSSL_LIBRARIES} ${platform_link_flags})
