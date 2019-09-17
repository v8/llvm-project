//===-- SymbolVendorWASM.cpp ----------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "SymbolVendorWASM.h"

#include <string.h>

#include "lldb/Core/Module.h"
#include "lldb/Core/ModuleSpec.h"
#include "lldb/Core/PluginManager.h"
#include "lldb/Core/Section.h"
#include "lldb/Host/Host.h"
#include "lldb/Symbol/LocateSymbolFile.h"
#include "lldb/Symbol/ObjectFile.h"
#include "lldb/Target/Target.h"
#include "lldb/Utility/StreamString.h"
#include "lldb/Utility/Timer.h"

using namespace lldb;
using namespace lldb_private;

// SymbolVendorWASM constructor
SymbolVendorWASM::SymbolVendorWASM(const lldb::ModuleSP &module_sp)
    : SymbolVendor(module_sp) {}

// Destructor
SymbolVendorWASM::~SymbolVendorWASM() {}

void SymbolVendorWASM::Initialize() {
  PluginManager::RegisterPlugin(GetPluginNameStatic(),
                                GetPluginDescriptionStatic(), CreateInstance);
}

void SymbolVendorWASM::Terminate() {
  PluginManager::UnregisterPlugin(CreateInstance);
}

lldb_private::ConstString SymbolVendorWASM::GetPluginNameStatic() {
  static ConstString g_name("WASM");
  return g_name;
}

const char *SymbolVendorWASM::GetPluginDescriptionStatic() {
  return "Symbol vendor for WASM that looks for dwo files that match "
         "executables.";
}

// CreateInstance
//
// Platforms can register a callback to use when creating symbol vendors to
// allow for complex debug information file setups, and to also allow for
// finding separate debug information files.
SymbolVendor *
SymbolVendorWASM::CreateInstance(const lldb::ModuleSP &module_sp,
                                 lldb_private::Stream *feedback_strm) {
  if (!module_sp)
    return nullptr;

  ObjectFile *obj_file = module_sp->GetObjectFile();
  if (!obj_file)
    return nullptr;

  static Timer::Category func_cat(LLVM_PRETTY_FUNCTION);
  Timer scoped_timer(func_cat, "SymbolVendorWASM::CreateInstance (module = %s)",
                     module_sp->GetFileSpec().GetPath().c_str());

  ModuleSpec module_spec;
  module_spec.GetFileSpec() = obj_file->GetFileSpec();

  std::string symbol_file_spec(
      module_spec.GetFileSpec().GetFileNameStrippingExtension().AsCString());
  symbol_file_spec += ".dwo";
  const FileSpec fspec(symbol_file_spec.c_str());

  FileSystem::Instance().Resolve(module_spec.GetFileSpec());
  module_spec.GetSymbolFileSpec() = fspec;
  module_spec.GetUUID() = obj_file->GetUUID();
  FileSpecList search_paths = Target::GetDefaultDebugFileSearchPaths();
  FileSpec dsym_fspec =
      Symbols::LocateExecutableSymbolFile(module_spec, search_paths);
  if (!dsym_fspec)
    return nullptr;

  DataBufferSP dsym_file_data_sp;
  lldb::offset_t dsym_file_data_offset = 0;
  ObjectFileSP dsym_objfile_sp = ObjectFile::FindPlugin(
      module_sp, &dsym_fspec, 0, FileSystem::Instance().GetByteSize(dsym_fspec),
      dsym_file_data_sp, dsym_file_data_offset);
  if (!dsym_objfile_sp)
    return nullptr;

  // This objfile is for debugging purposes. Sadly, ObjectFileWASM won't
  // be able to figure this out consistently as the symbol file may not
  // have stripped the code sections, etc.
  dsym_objfile_sp->SetType(ObjectFile::eTypeDebugInfo);

  SymbolVendorWASM *symbol_vendor = new SymbolVendorWASM(module_sp);

  // Get the module unified section list and add our debug sections to
  // that.
  SectionList *module_section_list = module_sp->GetSectionList();
  SectionList *objfile_section_list = dsym_objfile_sp->GetSectionList();

  static const SectionType g_sections[] = {
      eSectionTypeDWARFDebugAbbrev,   eSectionTypeDWARFDebugAddr,
      eSectionTypeDWARFDebugAranges,  eSectionTypeDWARFDebugCuIndex,
      eSectionTypeDWARFDebugFrame,    eSectionTypeDWARFDebugInfo,
      eSectionTypeDWARFDebugLine,     eSectionTypeDWARFDebugLoc,
      eSectionTypeDWARFDebugMacInfo,  eSectionTypeDWARFDebugPubNames,
      eSectionTypeDWARFDebugPubTypes, eSectionTypeDWARFDebugRanges,
      eSectionTypeDWARFDebugStr,      eSectionTypeDWARFDebugStrOffsets,
      eSectionTypeELFSymbolTable,     eSectionTypeDWARFGNUDebugAltLink,
  };
  for (SectionType section_type : g_sections) {
    if (SectionSP section_sp =
            objfile_section_list->FindSectionByType(section_type, true)) {
      if (SectionSP module_section_sp =
              module_section_list->FindSectionByType(section_type, true))
        module_section_list->ReplaceSection(module_section_sp->GetID(),
                                            section_sp);
      else
        module_section_list->AddSection(section_sp);
    }
  }

  symbol_vendor->AddSymbolFileRepresentation(dsym_objfile_sp);
  return symbol_vendor;
}

// PluginInterface protocol
ConstString SymbolVendorWASM::GetPluginName() { return GetPluginNameStatic(); }

uint32_t SymbolVendorWASM::GetPluginVersion() { return 1; }
