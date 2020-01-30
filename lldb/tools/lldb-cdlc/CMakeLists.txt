find_package(Protobuf REQUIRED)
include(FindPkgConfig)
pkg_search_module(GRPC REQUIRED grpc++)

find_program(GRPC_PLUGIN grpc_cpp_plugin)
set(PROTOC $<TARGET_FILE:protobuf::protoc>)
set(PROTO_SOURCES)


function(add_proto name)
  cmake_parse_arguments(ARG "GRPC" "" "" ${ARGN})
  set(PROTO_HDRS
    ${CMAKE_CURRENT_BINARY_DIR}/${name}.pb.h
    )
  set(PROTO_SRCS
    ${CMAKE_CURRENT_BINARY_DIR}/${name}.pb.cc
    )
  if (ARG_GRPC)
    set(PROTO_HDRS ${PROTO_HDRS}
      ${CMAKE_CURRENT_BINARY_DIR}/cdlc.grpc.pb.h
      )
    set(PROTO_SRCS ${PROTO_SRCS}
      ${CMAKE_CURRENT_BINARY_DIR}/cdlc.grpc.pb.cc
      )
  endif()
  set(PROTO_SOURCES ${PROTO_SOURCES} ${PROTO_SRCS} PARENT_SCOPE)
  add_custom_command(
    OUTPUT ${PROTO_SRCS} ${PROTO_HDRS}
    COMMAND ${PROTOC}
    ARGS
    --grpc_out ${CMAKE_CURRENT_BINARY_DIR}
    --cpp_out ${CMAKE_CURRENT_BINARY_DIR}
    -I ${CMAKE_CURRENT_SOURCE_DIR}
    --plugin=protoc-gen-grpc=${GRPC_PLUGIN}
    ${CMAKE_CURRENT_SOURCE_DIR}/${name}.proto
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/${name}.proto
    )
endfunction()

set(LLDB_CDLC_TOOL_DIR ${CMAKE_CURRENT_BINARY_DIR})
configure_file(lldb-cdlc-config.h.in
               ${CMAKE_CURRENT_BINARY_DIR}/lldb-cdlc-config.h @ONLY)

add_proto(cdlc GRPC)
add_library(LLDBCDLanguageComponent OBJECT
  Modules.cpp
  RPC.cpp
  Variables.cpp
  ${PROTO_SOURCES}
  )

get_target_property(LLDB_CDLC_SOURCES LLDBCDLanguageComponent SOURCES)
set(LLVM_OPTIONAL_SOURCES ${LLVM_OPTIONAL_SOURCES} ${LLDB_CDLC_SOURCES})

if (NOT LLVM_REQUIRES_EH AND NOT LLVM_REQUIRES_RTTI)
  target_compile_options(LLDBCDLanguageComponent PUBLIC -fno-rtti)
endif()

target_link_libraries(LLDBCDLanguageComponent PUBLIC
  ${PROTOBUF_LIBRARIES}
  ${GRPC_LIBRARIES}
  lldWasm
  lldDriver
  clangDaemon
  )
get_target_property(LLD_INCLUDE_DIRS lldDriver INCLUDE_DIRECTORIES)
get_target_property(CLANGD_INCLUDE_DIR clangDaemon SOURCE_DIR)
target_include_directories(LLDBCDLanguageComponent PUBLIC
  ../../source
  ${CLANGD_INCLUDE_DIR}
  ${GRPC_INCLUDE_DIRS}
  ${LLD_INCLUDE_DIRS}
  ${PROTOBUF_INCLUDE_DIRS}
  ${CMAKE_CURRENT_BINARY_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}
  )

target_compile_options(LLDBCDLanguageComponent PUBLIC
  -DGOOGLE_PROTOBUF_NO_RTTI
  )


add_lldb_tool(lldb-cdlc
  Main.cpp

  LINK_LIBS
  LLDBCDLanguageComponent
  lldbBase
  lldbHost
  lldbInitialization
  lldbPluginSymbolFileDWARF
  lldbPluginObjectFileWasm
  lldbPluginObjectFileELF
  lldbPluginSymbolVendorWasm
  lldbPluginInstructionARM
  lldbPluginInstructionMIPS
  lldbPluginInstructionMIPS64
  ${LLDB_SYSTEM_LIBS}

  LINK_COMPONENTS
  Support
  Core
  Analysis
  Linker
  IRReader
  Target
  WebAssembly 
  TransformUtils
  )

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
  set(optargs -O1)
else()
  set(optargs -O3 -DNDEBUG)
endif()

add_custom_target(CDLCRuntime
  COMMAND clang++ ${CMAKE_CURRENT_SOURCE_DIR}/runtime/formatters.cc
  -std=c++14
  --target=wasm32 -emit-llvm ${optargs} -c -I/usr/include//x86_64-linux-gnu
  -I/usr/include
  -I/usr/include/c++/8 -I/usr/include/x86_64-linux-gnu/c++/8
  DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/runtime/formatters.cc
  BYPRODUCTS formatters.bc
  )
add_dependencies(LLDBCDLanguageComponent CDLCRuntime)
