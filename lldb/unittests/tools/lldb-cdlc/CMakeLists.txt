include_directories(
  #  ${LLDB_SOURCE_DIR}/tools/lldb-cdlc/
  #  ${LLDB_BINARY_DIR}/tools/lldb-cdlc
  ${CMAKE_CURRENT_BINARY_DIR}
  ${LLD_INCLUDE_DIRS}
  )

set(LLDB_CDLC_TEST_INPUTS_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/inputs)
configure_file(lldb-cdlc-test-config.h.in
               ${CMAKE_CURRENT_BINARY_DIR}/lldb-cdlc-test-config.h @ONLY)

add_definitions(-DGOOGLE_PROTOBUF_NO_RTTI)

get_target_property(CDLC_RUNTIME_DIR LLDBCDLanguageComponent SOURCE_DIR)
set(CDLC_RUNTIME_DIR ${CDLC_RUNTIME_DIR}/runtime)

add_lldb_unittest(LLDBCDLCTests
  OffsetComputations.cpp
  VariablePrinting.cpp

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
  lldDriver
  lldWasm

  LINK_COMPONENTS
  Support
  Core
  Linker
  Analysis
  IRReader
  Target
  WebAssembly 
  TransformUtils
  )
