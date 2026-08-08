cmake_minimum_required(VERSION 3.25)

project(ChessHack LANGUAGES C CXX RC)

if(NOT WIN32)
    message(FATAL_ERROR "This project must be built for Windows.")
endif()

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "Build the DLL as x64; the recovered layouts are 64-bit.")
endif()

add_library(ChessHack SHARED
    dllmain.cpp
    heart.rc
    3rdparty/safetyhook.cpp
    3rdparty/Zydis.c
)

target_include_directories(ChessHack PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/3rdparty"
)

target_compile_features(ChessHack PRIVATE
    cxx_std_23
)

target_compile_definitions(ChessHack PRIVATE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    UNICODE
    _UNICODE
)

target_link_libraries(ChessHack PRIVATE
    Comctl32
    d3d9
    Psapi
    Ws2_32
)

set_target_properties(ChessHack PROPERTIES
    OUTPUT_NAME "chess_hack"
)

set_property(
    TARGET ChessHack
    PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE
)

set_property(
    TARGET ChessHack
    PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELWITHDEBINFO TRUE
)

set_property(
    TARGET ChessHack
    PROPERTY INTERPROCEDURAL_OPTIMIZATION_MINSIZEREL TRUE
)

if(MSVC)

    target_compile_options(ChessHack PRIVATE
        /W4
        /permissive-
        /EHsc
        /utf-8
        $<$<CONFIG:Release,RelWithDebInfo>:
            /O2
            /Oi
            /Gy
            /Gw
            /GF
            /Zc:inline
        >
        $<$<CONFIG:MinSizeRel>:
            /O1
            /Oi
            /Gy
            /Gw
            /GF
            /Zc:inline
        >
    )

    target_link_options(ChessHack PRIVATE

        $<$<CONFIG:Release,RelWithDebInfo,MinSizeRel>:
            /OPT:REF
            /OPT:ICF
            /INCREMENTAL:NO
        >
    )

    set_property(
        TARGET ChessHack
        PROPERTY MSVC_RUNTIME_LIBRARY
        "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL"
    )

endif()
