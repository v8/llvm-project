#include "Modules.h"
#include "Util.h"

#include "lldb/Core/Module.h"
#include "lldb/Core/StreamFile.h"
#include "lldb/Core/Value.h"
#include "lldb/Core/dwarf.h"
#include "lldb/Symbol/Block.h"
#include "lldb/Symbol/CompileUnit.h"
#include "lldb/Symbol/LineTable.h"
#include "lldb/Symbol/SymbolFile.h"
#include "lldb/Symbol/Type.h"
#include "lldb/Symbol/TypeList.h"
#include "lldb/Symbol/VariableList.h"
#include "lldb/Utility/ArchSpec.h"
#include "lldb/Utility/FileSpec.h"
#include "lldb/Utility/RegularExpression.h"
#include "lldb/Utility/Stream.h"
#include "lldb/lldb-enumerations.h"
#include "lldb/lldb-forward.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/Optional.h"
#include "llvm/ADT/SmallSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/MD5.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/raw_ostream.h"
#include <memory>

namespace llvm {
struct PathSubstitutionParser
    : llvm::cl::parser<std::pair<std::string, std::string>> {
  using llvm::cl::parser<std::pair<std::string, std::string>>::parser;

  bool parse(llvm::cl::Option &O, llvm::StringRef ArgName,
             const std::string &ArgValue,
             std::pair<std::string, std::string> &Result) {
    assert(ArgName == "url-subst");
    auto First = ArgValue.find('=');
    if (First == std::string::npos)
      return O.error(
          "Substitution syntax: 'find=replace'. No = found in argument '" +
          ArgValue + "'");
    auto Last = ArgValue.rfind('=');
    if (First != Last)
      return O.error("Substitution syntax: 'find=replace'. Multiple = found in "
                     "argument '" +
                     ArgValue + "'");
    if (First == 0)
      return O.error("Substitution syntax: 'find=replace'. 'find' is empty.");
    if (First == ArgValue.length() - 1)
      return O.error(
          "Substitution syntax: 'find=replace'. 'replace' is empty.");
    Result.first = {ArgValue, 0, First};
    Result.second = {ArgValue, First + 1, std::string::npos};
    return false;
  }
};
} // namespace llvm

llvm::cl::list<std::pair<std::string, std::string>, bool,
               llvm::PathSubstitutionParser>
    PathSubstitutions("url-subst");

namespace lldb_private {

#define INDEX_TRAITS(T)                                                        \
  template <> size_t index_traits<const T##List>::size(const T##List &C) {     \
    return C.GetSize();                                                        \
  }                                                                            \
  template <>                                                                  \
  auto index_traits<const T##List>::at(const T##List &C, size_t N) {           \
    return C.Get##T##AtIndex(N);                                               \
  }                                                                            \
  template <> size_t index_traits<T##List>::size(T##List &C) {                 \
    return C.GetSize();                                                        \
  }                                                                            \
  template <> auto index_traits<T##List>::at(T##List &C, size_t N) {           \
    return C.Get##T##AtIndex(N);                                               \
  }

template <> size_t index_traits<Module>::size(Module &C) {
  return C.GetNumCompileUnits();
}
template <> auto index_traits<Module>::at(Module &C, size_t N) {
  return C.GetCompileUnitAtIndex(N);
}

INDEX_TRAITS(FileSpec);
INDEX_TRAITS(Variable);

} // namespace lldb_private

namespace lldb {
namespace cdlc {

static llvm::SmallString<32>
getFilename(llvm::StringRef Url,
            llvm::Optional<llvm::StringRef> UrlBase = llvm::None) {
  if (Url.startswith("file://") || Url.startswith("wasm://")) {
    auto WithoutSchema = Url.drop_front(7);
    if (!llvm::sys::path::is_absolute(WithoutSchema) && UrlBase) {
      llvm::SmallString<32> Result = *UrlBase;
      llvm::sys::path::append(Result, WithoutSchema);
      return Result;
    }
    return WithoutSchema;
  }
  llvm::SmallString<32> Result = Url;
  for (auto Substitution : PathSubstitutions) {
    llvm::errs() << "Fixing path '" << Result << ": replacing '"
                 << Substitution.first << "' with '" << Substitution.second
                 << "'\n";
    llvm::sys::path::replace_path_prefix(Result, Substitution.first,
                                         Substitution.second);
  }
  return Result;
}

std::shared_ptr<WasmModule>
WasmModule::createFromUrl(llvm::StringRef Id, llvm::StringRef Url,
                          llvm::Optional<llvm::StringRef> SymbolsFile) {

  auto Filename = getFilename(Url);
  if (Filename.empty()) {
    llvm::errs() << "Can only handle file:// urls currently, ignoring '" << Url
                 << "'\n";
    return {};
  }
  llvm::errs() << "Loading module from '" << Filename << "'\n";

  std::shared_ptr<WasmModule> NewModule(new WasmModule(Id));

  NewModule->Module = std::make_shared<lldb_private::Module>(
      lldb_private::FileSpec(Filename),
      lldb_private::ArchSpec("wasm32-unknown-unknown-wasm"));

  if (SymbolsFile)
    NewModule->Module->SetSymbolFileFileSpec(
        lldb_private::FileSpec(getFilename(*SymbolsFile, {Filename})));
  NewModule->Module->PreloadSymbols();

  return NewModule;
}

std::shared_ptr<WasmModule>
WasmModule::createFromCode(llvm::StringRef Id, llvm::StringRef ByteCode,
                           llvm::Optional<llvm::StringRef> SymbolsFile) {

  llvm::SmallString<128> TFile;
  llvm::sys::path::system_temp_directory(true, TFile);
  llvm::sys::path::append(TFile, "%%%%%%.wasm");

  auto TF = llvm::sys::fs::TempFile::create(TFile);
  if (!TF) {
    llvm::errs() << "Failed to create temporary file for module\n";
    return {};
  }

  llvm::StringRef Filename = TF->TmpName;
  llvm::errs() << "Created temporary module " << Filename << "\n";
  llvm::raw_fd_ostream O(TF->FD, false);
  O << ByteCode;

  auto NewModule = createFromUrl(Id, ("file://" + Filename).str(), SymbolsFile);
  NewModule->TempModule = std::move(*TF);
  return NewModule;
}

WasmModule::~WasmModule() {
  if (TempModule)
    consumeError(TempModule->discard());
}

bool WasmModule::valid() const { return Module->GetNumCompileUnits() > 0; }

llvm::SmallVector<std::string, 1> WasmModule::getSourceScripts() const {
  llvm::SmallVector<std::string, 1> CUs;
  llvm::SmallSet<std::pair<llvm::StringRef, llvm::StringRef>, 1> AllFiles;
  for (size_t Idx = 0; Idx < Module->GetNumCompileUnits(); Idx++) {
    auto CU = Module->GetCompileUnitAtIndex(Idx);
    for (auto F : indexed(CU->GetSupportFiles())) {
      auto Dir = F.GetDirectory().GetStringRef();
      auto Filename = F.GetFilename().GetStringRef();
      if (Filename.empty()) // FIXME What's going on here?
        continue;
      if (!AllFiles.insert(std::make_pair(Dir, Filename)).second)
        continue;
      CUs.emplace_back(F.GetPath());
    }
  }
  return CUs;
}

static void getVariablesFromOffset(lldb_private::Module &Module,
                                   lldb::addr_t Offset,
                                   lldb_private::VariableList &VarList) {
  if (!Module.GetObjectFile() || !Module.GetObjectFile()->GetSectionList())
    return;
  lldb_private::SymbolContext Sc;
  SectionSP Section =
      Module.GetObjectFile()->GetSectionList()->FindSectionByType(
          lldb::eSectionTypeCode, false);
  lldb_private::Address Addr(Section, Offset);
  auto Resolved =
      Module.ResolveSymbolContextForAddress(Addr, eSymbolContextBlock, Sc);
  if ((Resolved & eSymbolContextBlock) == eSymbolContextBlock) {
    Sc.block->AppendVariables(
        true, true, false, [](lldb_private::Variable *) { return true; },
        &VarList);
  }
}

static VariableSP findVariableAtOffset(lldb_private::Module &Module,
                                       lldb::addr_t Offset,
                                       llvm::StringRef Name) {
  lldb_private::VariableList VarList;
  getVariablesFromOffset(Module, Offset, VarList);
  for (auto Var : indexed(VarList)) {
    if (Var->GetName().GetStringRef() == Name)
      return Var;
  }
  VarList.Clear();
  Module.FindGlobalVariables(lldb_private::RegularExpression(Name), 1, VarList);
  if (!VarList.Empty())
    return VarList.GetVariableAtIndex(0);
  return {};
}

static llvm::SmallVector<MemoryLocation, 1>
getVariableLocation(lldb_private::Module &Module, lldb::addr_t FrameOffset,
                    llvm::StringRef Name) {
  llvm::SmallVector<MemoryLocation, 1> Result;
  VariableSP Var = findVariableAtOffset(Module, FrameOffset, Name);
  if (!Var)
    return {};
  auto &E = Var->LocationExpression();
  lldb_private::DataExtractor Opcodes;
  E.GetExpressionData(Opcodes);
  lldb::offset_t Offset = 0;
  const lldb::offset_t EndOffset = Opcodes.GetByteSize();

  auto TypeName = Var->GetType()->GetQualifiedName();

  // FIXME: Revisit this once we figured out how to actually evaluate
  // variables, since it doesn't handle expressions beyond constants and
  // symbols.
  while (Opcodes.ValidOffset(Offset) && Offset < EndOffset) {
    const uint8_t Op = Opcodes.GetU8(&Offset);
    switch (Op) {
    case DW_OP_plus_uconst: {
      const uint64_t Variable = Opcodes.GetULEB128(&Offset);
      auto &MemLocation = Result.emplace_back();
      MemLocation.Type = TypeName.GetCString();
      MemLocation.AddressSpace = 0;
      MemLocation.Offset = Variable;
      break;
    }
    case DW_OP_WASM_location: {
      uint64_t MemType = Opcodes.GetULEB128(&Offset);
      uint64_t Variable = Opcodes.GetULEB128(&Offset);
      auto &MemLocation = Result.emplace_back();
      MemLocation.Type = TypeName.GetCString();
      MemLocation.AddressSpace = MemType + 1;
      MemLocation.Offset = Variable;
      break;
    }
    case DW_OP_addr: {
      uint64_t Address = Opcodes.GetAddress(&Offset);
      auto &MemLocation = Result.emplace_back();
      MemLocation.Type = TypeName.GetCString();
      MemLocation.AddressSpace = 0;
      MemLocation.Offset = Address;
      break;
    }
    default:
      llvm_unreachable("Unhandled wasm tag");
    }
  }
  return Result;
}

/*
static SourceFile *convert(const lldb_private::FileSpec &File) {
  auto SF = new SourceFile();
  SF->set_filename(File.GetFilename().GetStringRef());
  SF->set_directory(File.GetDirectory().GetStringRef());
  return SF;
}

static lldb_private::FileSpec convert(const SourceFile &File) {
  lldb_private::FileSpec FS;
  FS.GetFilename() = lldb_private::ConstString(File.filename());
  FS.GetDirectory() = lldb_private::ConstString(File.directory());
  return FS;
}
*/

llvm::SmallVector<SourceLocation, 1>
WasmModule::getSourceLocationFromOffset(lldb::addr_t Offset) const {
  llvm::SmallVector<SourceLocation, 1> Lines;

  for (CompUnitSP CU : indexed(*Module)) {
    lldb_private::LineTable *LT = CU->GetLineTable();
    lldb_private::LineEntry LE;
    SectionSP Section =
        Module->GetObjectFile()->GetSectionList()->FindSectionByType(
            lldb::eSectionTypeCode, false);
    lldb_private::Address Addr(Section, Offset);
    if (LT->FindLineEntryByAddress(Addr, LE))
      if (LE.line > 0 && LE.column > 0)
        Lines.emplace_back(LE.file.GetPath(), LE.line, LE.column);
  }
  return Lines;
}

llvm::SmallVector<lldb::addr_t, 1>
WasmModule::getOffsetFromSourceLocation(const SourceLocation &SourceLoc) const {
  llvm::SmallVector<lldb::addr_t, 1> Locations;
  std::vector<lldb_private::Address> OutputLocal, OutputExtern;

  for (CompUnitSP CU : indexed(*Module)) {
    lldb_private::SymbolContextList List;
    CU->ResolveSymbolContext(lldb_private::FileSpec(SourceLoc.File),
                             SourceLoc.Line, true, true,
                             eSymbolContextEverything, List);
    for (uint32_t I = 0; I < List.GetSize(); I++) {
      lldb_private::SymbolContext Sc;
      if (List.GetContextAtIndex(I, Sc) && Sc.line_entry.IsValid()) {
        llvm::errs() << "Got location: " << Sc.line_entry.line << ":"
                     << Sc.line_entry.column << " ("
                     << Sc.line_entry.is_terminal_entry << ")"
                     << "\n";
        if (Sc.line_entry.line == SourceLoc.Line)
          Locations.push_back(Sc.line_entry.range.GetBaseAddress().GetOffset());
      }
    }
  }
  return Locations;
}

const WasmModule *ModuleCache::findModule(llvm::StringRef ScriptId) const {
  auto I = Modules.find(ScriptId);
  if (I != Modules.end())
    return I->second.get();
  return nullptr;
}

bool ModuleCache::deleteModule(llvm::StringRef ScriptId) {
  return Modules.erase(ScriptId) > 0;
}

static llvm::SmallString<32>
moduleHash(const Binary &Code, llvm::Optional<llvm::StringRef> SymbolsFile) {
  llvm::MD5 Hash;
  llvm::MD5::MD5Result HashResult;
  Hash.update(Code);
  if (SymbolsFile)
    Hash.update(*SymbolsFile);
  Hash.final(HashResult);
  return HashResult.digest();
}

const WasmModule *
ModuleCache::getModuleFromUrl(llvm::StringRef Id, llvm::StringRef Url,
                              llvm::Optional<llvm::StringRef> SymbolsFile) {
  if (auto M = findModule(Id))
    return M;

  auto Hash = moduleHash(Url, SymbolsFile);
  auto I = ModuleHashes.find(Hash);
  if (I != ModuleHashes.end()) {
    llvm::errs() << "Cache hit for module '" << Id << "' at " << Url << "\n";
    Modules[Id] = I->second;
    return I->second.get();
  }

  auto Module =
      Modules.emplace(Id, WasmModule::createFromUrl(Id, Url, SymbolsFile))
          .first->second;
  if (Module) {
    llvm::errs() << "Loaded module " << Id << " with "
                 << Module->getSourceScripts().size() << " source files\n";
    ModuleHashes[Hash] = Module;
  }
  return Module.get();
}

const WasmModule *
ModuleCache::getModuleFromCode(llvm::StringRef Id, llvm::StringRef ByteCode,
                               llvm::Optional<llvm::StringRef> SymbolsFile) {
  if (auto M = findModule(Id))
    return M;

  auto Hash = moduleHash(ByteCode, SymbolsFile);
  auto I = ModuleHashes.find(Hash);
  if (I != ModuleHashes.end()) {
    Modules[Id] = I->second;
    return I->second.get();
  }

  auto Module =
      Modules.emplace(Id, WasmModule::createFromCode(Id, ByteCode, SymbolsFile))
          .first->second;
  if (Module) {
    llvm::errs() << "Loaded module " << Id << " with "
                 << Module->getSourceScripts().size() << " source files\n";
    ModuleHashes[Hash] = Module;
  }
  return Module.get();
}

llvm::SmallVector<const WasmModule *, 1>
ModuleCache::findModulesContainingSourceScript(llvm::StringRef File) const {
  lldb_private::FileSpec ScriptSpec(File);
  llvm::SmallVector<const WasmModule *, 1> FoundModules;
  for (auto &KV : Modules) {
    auto &M = KV.second;
    for (auto CU : indexed(*M->Module)) {
      if (CU->GetSupportFiles().FindFileIndex(0, ScriptSpec, true) !=
          UINT32_MAX) {
        llvm::errs() << "Found " << CU->GetPrimaryFile().GetPath() << "\n";
        FoundModules.push_back(M.get());
        break;
      }
    }
  }
  return FoundModules;
}

llvm::SmallVector<Variable, 1>
WasmModule::getVariablesInScope(lldb::addr_t Offset) const {
  llvm::SmallVector<Variable, 1> Variables;
  lldb_private::VariableList VarList;
  getVariablesFromOffset(*Module, Offset, VarList);
  llvm::errs() << "Found " << VarList.GetSize() << " variables in scope and ";
  Module->FindGlobalVariables(lldb_private::RegularExpression(".*"), -1,
                              VarList);
  llvm::errs() << VarList.GetSize() << " globals\n.";
  for (auto Var : indexed(VarList)) {
    Variables.emplace_back(Var->GetName().GetStringRef(), Var->GetScope(),
                           Var->GetType()->GetQualifiedName().GetStringRef());
  }
  return Variables;
}

static VariablePrinter Formatter;
llvm::Expected<Binary>
WasmModule::getVariableFormatScript(llvm::StringRef Name,
                                    const MemoryLocation &Loc) const {
  lldb_private::TypeList TL;
  llvm::DenseSet<lldb_private::SymbolFile *> SearchedSymbolFiles;
  Module->FindTypes(lldb_private::ConstString(Loc.Type), true, 1,
                    SearchedSymbolFiles, TL);
  if (TL.GetSize() == 0)
    return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                   "Unable to find type '%s'",
                                   Name.str().c_str());
  auto Code = Formatter.generateModule(
      Name, TL.GetTypeAtIndex(0)->GetFullCompilerType(), Loc);
  if (!Code)
    return Code.takeError();
  auto WasmCode = Formatter.generateCode(**Code);
  return WasmCode->getBuffer();
}

llvm::Expected<Binary>
WasmModule::getVariableFormatScript(llvm::StringRef Name,
                                    lldb::addr_t FrameOffset) const {
  VariableSP Variable = findVariableAtOffset(*Module, FrameOffset, Name);
  if (!Variable)
    return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                   "Variable '%s' not found at offset %zu",
                                   Name.str().c_str(), FrameOffset);
  auto Location = getVariableLocation(*Module, FrameOffset, Name);
  lldb_private::StreamFile Errs(2, false);
  Variable->GetType()->Dump(&Errs, true);
  lldb_private::Type *VarTy = Variable->GetType();

  assert(Location.size() >= 1u && "Hmm.");
  auto Code = Formatter.generateModule(Name, VarTy->GetFullCompilerType(),
                                       Location.front());
  if (!Code)
    return Code.takeError();
  auto WasmCode = Formatter.generateCode(**Code);
  return WasmCode->getBuffer();
}

} // namespace cdlc
} // namespace lldb
