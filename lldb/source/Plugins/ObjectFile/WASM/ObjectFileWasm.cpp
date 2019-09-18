//===-- ObjectFileWASM.cpp -------------------------------- -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//


#include "Plugins/ObjectFile/WASM/ObjectFileWasm.h"
#include "lldb/Core/ModuleSpec.h"
#include "lldb/Core/PluginManager.h"
#include "lldb/Core/Section.h"

#include "lldb/Target/SectionLoadList.h"
#include "lldb/Target/Target.h"

using namespace lldb;
using namespace lldb_private;
using namespace lldb_private::wasm;

void ObjectFileWASM::Initialize() {
  PluginManager::RegisterPlugin(GetPluginNameStatic(),
                                GetPluginDescriptionStatic(), CreateInstance,
                                CreateMemoryInstance, GetModuleSpecifications);
}

void ObjectFileWASM::Terminate() {
  PluginManager::UnregisterPlugin(CreateInstance);
}

ConstString ObjectFileWASM::GetPluginNameStatic() {
  static ConstString g_name("wasm");
  return g_name;
}

ObjectFile *
ObjectFileWASM::CreateInstance(const ModuleSP &module_sp, DataBufferSP &data_sp,
                               offset_t data_offset, const FileSpec *file,
                               offset_t file_offset, offset_t length) {
  return new ObjectFileWASM(module_sp, data_sp, data_offset, file, file_offset,
                            length);
}

ObjectFile *ObjectFileWASM::CreateMemoryInstance(const ModuleSP &module_sp,
                                                 DataBufferSP &data_sp,
                                                 const ProcessSP &process_sp,
                                                 addr_t header_addr) {
  return nullptr;
}

size_t ObjectFileWASM::GetModuleSpecifications(
    const FileSpec &file, DataBufferSP &data_sp, offset_t data_offset,
    offset_t file_offset, offset_t length, ModuleSpecList &specs) {
  if (data_sp->GetByteSize() < 4) {
    return 0;
  }

  uint32_t magic_number = *(uint32_t *)data_sp->GetBytes();
  if (magic_number != 0x6d736100) { // '\0asm'
    return 0;
  }

  ModuleSpec spec(file, ArchSpec("wasm32-unknown-unknown-wasm"));
  specs.Append(spec);
  return 1;
}

ObjectFileWASM::ObjectFileWASM(const ModuleSP &module_sp, DataBufferSP &data_sp,
                               offset_t data_offset, const FileSpec *file,
                               offset_t offset, offset_t length)
    : ObjectFile(module_sp, file, offset, length, data_sp, data_offset),
      m_arch("wasm32-unknown-unknown-wasm") {
  uint8_t bytes[16] = {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
                       0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f};
  m_uuid = UUID::fromData(bytes, 16);
}

bool ObjectFileWASM::ParseHeader() {
  // We already parsed the header during initialization.
  return true;
}

Symtab *ObjectFileWASM::GetSymtab() {
  // TODO
  return nullptr;
}

void ObjectFileWASM::CreateSections(SectionList &unified_section_list) {
  if (m_sections_up)
    return;

  m_sections_up = std::make_unique<SectionList>();
  SectionSP section_sp(
      new Section(GetModule(), // Module to which this section belongs.
                  this, // ObjectFile to which this section belongs and should
                        // read section data from.
                  1,    // Section ID.
                  ConstString("code"), // Section name.
                  eSectionTypeCode,    // Section type.
                  0x00000000,          // VM address.
                  0x0fffffff,          // VM size in bytes of this section.
                  0,                   // Offset of this section in the file.
                  0x0fffffff, // Size of the section as found in the file.
                  0,          // Alignment of the section
                  0,          // Flags for this section.
                  1));        // Number of host bytes per target byte
  m_sections_up->AddSection(section_sp);

  unified_section_list = *m_sections_up;
}

bool ObjectFileWASM::SetLoadAddress(Target &target, lldb::addr_t value,
                                    bool value_is_offset) {
  ModuleSP module_sp = GetModule();
  if (module_sp) {
    size_t num_loaded_sections = 0;
    SectionList *section_list = GetSectionList();
    if (section_list) {
      const size_t num_sections = section_list->GetSize();
      size_t sect_idx = 0;

      for (sect_idx = 0; sect_idx < num_sections; ++sect_idx) {
        SectionSP section_sp(section_list->GetSectionAtIndex(sect_idx));
        if (target.GetSectionLoadList().SetSectionLoadAddress(section_sp,
                                                              value)) {
          ++num_loaded_sections;
        }
      }
      return num_loaded_sections > 0;
    }
  }
  return false;
}

void ObjectFileWASM::RelocateSection(lldb_private::Section *section) {}