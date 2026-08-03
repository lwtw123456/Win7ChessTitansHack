cmake_minimum_required(VERSION 3.25)

project(ChessEngineHook LANGUAGES C CXX)

if(NOT WIN32)
    message(FATAL_ERROR "This project must be built for Windows.")
endif()

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "Build the DLL as x64; the recovered layouts are 64-bit.")
endif()

add_library(ChessEngineHook SHARED
    dllmain.cpp
    3rdparty/safetyhook.cpp
    3rdparty/Zydis.c
)

set_target_properties(ChessEngineHook PROPERTIES
    OUTPUT_NAME "chess_hack"
)

target_include_directories(ChessEngineHook PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/3rdparty"
)

target_compile_features(ChessEngineHook PRIVATE
    cxx_std_23
)

target_compile_definitions(ChessEngineHook PRIVATE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    UNICODE
    _UNICODE
)

target_link_libraries(ChessEngineHook PRIVATE
    Comctl32
    Psapi
    Ws2_32
)

include(CheckIPOSupported)

check_ipo_supported(
    RESULT IPO_SUPPORTED
    OUTPUT IPO_ERROR
    LANGUAGES C CXX
)

if(IPO_SUPPORTED)
    set_property(TARGET ChessEngineHook PROPERTY
        INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE
    )

    set_property(TARGET ChessEngineHook PROPERTY
        INTERPROCEDURAL_OPTIMIZATION_MINSIZEREL TRUE
    )
endif()

if(MSVC)
    target_compile_options(ChessEngineHook PRIVATE
        /W4
        /permissive-
        /EHsc
        /utf-8
        /Gy
        /Gw
        /GF
        /Oi
        /Zc:inline
        $<$<OR:$<CONFIG:Release>,$<CONFIG:MinSizeRel>>:/O1>
    )

    target_link_options(ChessEngineHook PRIVATE
        $<$<OR:$<CONFIG:Release>,$<CONFIG:MinSizeRel>>:/OPT:REF>
        $<$<OR:$<CONFIG:Release>,$<CONFIG:MinSizeRel>>:/OPT:ICF>
        $<$<OR:$<CONFIG:Release>,$<CONFIG:MinSizeRel>>:/INCREMENTAL:NO>
    )

    set_property(TARGET ChessEngineHook PROPERTY
        MSVC_RUNTIME_LIBRARY
        "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL"
    )
endif()