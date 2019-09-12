//===-- ObjectFileWasm.h ---------------------------------- -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_PLUGINS_OBJECTFILE_WASM_OBJECTFILEWASM_H
#define LLDB_PLUGINS_OBJECTFILE_WASM_OBJECTFILEWASM_H

#include "lldb/Symbol/ObjectFile.h"
#include "lldb/Utility/ArchSpec.h"

namespace lldb_private {
namespace wasm {

class ObjectFileWASM : public ObjectFile {
public:
  // Static Functions
  static void Initialize();
  static void Terminate();

  static ConstString GetPluginNameStatic();
  static const char *GetPluginDescriptionStatic() {
    return "WebAssembly object file reader.";
  }

  static ObjectFile *
  CreateInstance(const lldb::ModuleSP &module_sp, lldb::DataBufferSP &data_sp,
                 lldb::offset_t data_offset, const FileSpec *file,
                 lldb::offset_t file_offset, lldb::offset_t length);

  static ObjectFile *CreateMemoryInstance(const lldb::ModuleSP &module_sp,
                                          lldb::DataBufferSP &data_sp,
                                          const lldb::ProcessSP &process_sp,
                                          lldb::addr_t header_addr);

  static size_t GetModuleSpecifications(const FileSpec &file,
                                        lldb::DataBufferSP &data_sp,
                                        lldb::offset_t data_offset,
                                        lldb::offset_t file_offset,
                                        lldb::offset_t length,
                                        ModuleSpecList &specs);

  // PluginInterface protocol
  ConstString GetPluginName() override { return GetPluginNameStatic(); }

  uint32_t GetPluginVersion() override { return 1; }

  // ObjectFile Protocol.

  bool ParseHeader() override;

  lldb::ByteOrder GetByteOrder() const override {
    return m_arch.GetByteOrder();
  }

  bool IsExecutable() const override { return false; }

  uint32_t GetAddressByteSize() const override {
    return 4; // m_arch.GetAddressByteSize();
  }

  AddressClass GetAddressClass(lldb::addr_t file_addr) override {
    return AddressClass::eInvalid;
  }

  Symtab *GetSymtab() override;

  bool IsStripped() override { return true; }

  void CreateSections(SectionList &unified_section_list) override;

  void Dump(Stream *s) override {}

  ArchSpec GetArchitecture() override { return m_arch; }

  UUID GetUUID() override { return m_uuid; }

  uint32_t GetDependentModules(FileSpecList &files) override { return 0; }

  Type CalculateType() override { return eTypeExecutable; }

  Strata CalculateStrata() override { return eStrataUser; }

  bool SetLoadAddress(lldb_private::Target &target, lldb::addr_t value,
                      bool value_is_offset) override;

  void RelocateSection(lldb_private::Section *section) override;

private:
  ArchSpec m_arch;
  UUID m_uuid;
  ObjectFileWASM(const lldb::ModuleSP &module_sp, lldb::DataBufferSP &data_sp,
                 lldb::offset_t data_offset, const FileSpec *file,
                 lldb::offset_t offset, lldb::offset_t length);
};

} // namespace wasm
} // namespace lldb_private
#endif // LLDB_PLUGINS_OBJECTFILE_WASM_OBJECTFILEWASM_H
