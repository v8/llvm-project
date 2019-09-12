add_lldb_library(lldbPluginSymbolVendorWASM PLUGIN
  SymbolVendorWASM.cpp

  LINK_LIBS
    lldbCore
    lldbHost
    lldbSymbol
    lldbPluginObjectFileWASM
  )
