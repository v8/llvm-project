#include "Variables.h"
#include "Protocol.h"
#include "lldb-cdlc-config.h"

#include "lld/Common/Driver.h"
#include "lldb/Symbol/CompilerType.h"
#include "lldb/Utility/ConstString.h"
#include "llvm/ADT/Triple.h"
#include "llvm/Analysis/TargetTransformInfo.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/IR/Module.h"
#include "llvm/IRReader/IRReader.h"
#include "llvm/Linker/Linker.h"
#include "llvm/Support/Errc.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/ErrorOr.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/TargetRegistry.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Target/TargetOptions.h"
#include "llvm/Transforms/IPO/PassManagerBuilder.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include <iostream>
#include <system_error>

using namespace llvm;

namespace lldb {
namespace cdlc {

static Function *
createFunction(llvm::Module &M, const Twine &Name, Type *ReturnTy,
               std::initializer_list<std::pair<StringRef, Type *>> Arguments) {
  SmallVector<Type *, 1> ArgTys;
  for (auto &Arg : Arguments)
    ArgTys.push_back(Arg.second);

  auto *F =
      Function::Create(FunctionType::get(ReturnTy, ArgTys, false),
                       GlobalValue::LinkageTypes::ExternalLinkage, Name, M);

  for (auto Arg : zip_first(Arguments, F->args()))
    std::get<1>(Arg).setName(std::get<0>(Arg).first);
  return F;
}

// void __getLocal(uint64_t index, void* result);
// static FunctionCallee getGetLocalCallback(Module &M) {
//  IRBuilder<> B(M.getContext());
//  return M.getOrInsertFunction("__getLocal", B.getVoidTy(), B.getInt64Ty(),
//                               B.getInt64Ty(), B.getInt8PtrTy());
//}

// FIXME we can't use 64bit here because javascript ...
// void __getMemory(uint64_t offset, uint64_t size, void* result);
static FunctionCallee getGetMemoryCallback(llvm::Module &M) {
  IRBuilder<> B(M.getContext());
  return M.getOrInsertFunction("__getMemory", B.getVoidTy(), B.getInt32Ty(),
                               B.getInt32Ty(), B.getInt8PtrTy());
}

// int format_begin_array(const char* ArrayName, const char *ElementType,
//                        char *Buffer, int Size);
static FunctionCallee getArrayBeginFormatter(llvm::Module &M) {
  IRBuilder<> B(M.getContext());
  return M.getOrInsertFunction("format_begin_array", B.getInt32Ty(),
                               B.getInt8PtrTy(), B.getInt8PtrTy(),
                               B.getInt8PtrTy(), B.getInt32Ty());
}

// int format_sep(char *Buffer, int Size);
static FunctionCallee getSepFormatter(llvm::Module &M) {
  IRBuilder<> B(M.getContext());
  return M.getOrInsertFunction("format_sep", B.getInt32Ty(), B.getInt8PtrTy(),
                               B.getInt32Ty());
}

// int format_end_array(char *Buffer, int Size);
static FunctionCallee getArrayEndFormatter(llvm::Module &M) {
  IRBuilder<> B(M.getContext());
  return M.getOrInsertFunction("format_end_array", B.getInt32Ty(),
                               B.getInt8PtrTy(), B.getInt32Ty());
}

static Value *readVarValue(IRBuilder<> &Builder, const MemoryLocation &Variable,
                           Type *Ty) {
  llvm::Module &M = *Builder.GetInsertBlock()->getModule();
  switch (WasmAddressSpace(Variable.AddressSpace)) {
  case WasmAddressSpace::Memory: {
    FunctionCallee F = getGetMemoryCallback(M);
    Value *Result = Builder.CreateAlloca(Ty);
    Value *Args[] = {Builder.getInt32(Variable.Offset),
                     Builder.getInt32(M.getDataLayout().getTypeAllocSize(Ty)),
                     Builder.CreateBitCast(Result, Builder.getInt8PtrTy())};
    Builder.CreateCall(F, Args);
    return Result;
  }
  default:
    llvm_unreachable("Unhandled wasm address space");
  }
}

void handleError(IRBuilder<> &Builder, Value *ReturnValue) {
  Value *Cmp = Builder.CreateICmpSLT(ReturnValue, Builder.getInt32(0));
  assert(Builder.GetInsertPoint() != Builder.GetInsertBlock()->end());
  Instruction *IP = &*Builder.GetInsertPoint();
  llvm::SplitBlockAndInsertIfThen(Cmp, IP, true);
  Builder.SetInsertPoint(IP);
}

template <typename... ArgTs>
VariablePrinter::StringSlice
callFormatter(IRBuilder<> &Builder, FunctionCallee Formatter, Value *Buffer,
              Value *Size, ArgTs... Arguments) {
  Value *Args[] = {Arguments..., Buffer, Size};
  Value *Offset = Builder.CreateCall(Formatter, Args, "Offset");
  handleError(Builder, Offset);
  Size = Builder.CreateSub(Size, Offset, "Size");
  Buffer = Builder.CreateInBoundsGEP(Buffer, Offset, "Buffer");
  return std::make_pair(Buffer, Size);
}

Expected<VariablePrinter::StringSlice>
VariablePrinter::formatPrimitive(IRBuilder<> &Builder, Value *Buffer,
                                 Value *Size, StringRef Name,
                                 const lldb_private::CompilerType &VariableType,
                                 const MemoryLocation &Variable) {
  auto &M = *Builder.GetInsertBlock()->getModule();
  auto Formatter =
      PrimitiveFormatters.find(VariableType.GetTypeName().GetStringRef());
  if (Formatter == PrimitiveFormatters.end())
    return createStringError(llvm::inconvertibleErrorCode(),
                             "No formatter for type '%s'",
                             Variable.Type.c_str());
  auto F = Formatter->second(M);
  Value *VarValue = readVarValue(
      Builder, Variable,
      F.getFunctionType()->getParamType(0)->getPointerElementType());
  Value *VarName = Builder.CreateGlobalStringPtr(Name);

  return callFormatter(Builder, Formatter->second(M), Buffer, Size, VarValue,
                       VarName);
}

Expected<VariablePrinter::StringSlice>
VariablePrinter::formatAggregate(IRBuilder<> &Builder, Value *Buffer,
                                 Value *Size, StringRef Name,
                                 const lldb_private::CompilerType &VariableType,
                                 const MemoryLocation &Variable) {
  auto &M = *Builder.GetInsertBlock()->getModule();

  Value *TypeName =
      Builder.CreateGlobalStringPtr(VariableType.GetTypeName().GetStringRef());
  Value *VarName = Builder.CreateGlobalStringPtr(Name);

  std::tie(Buffer, Size) = callFormatter(Builder, getArrayBeginFormatter(M),
                                         Buffer, Size, VarName, TypeName);
  for (size_t Child = 0, E = VariableType.GetNumFields(); Child < E; ++Child) {
    if (Child > 0)
      std::tie(Buffer, Size) =
          callFormatter(Builder, getSepFormatter(M), Buffer, Size);

    std::string ChildName;
    uint64_t BitOffset;
    auto ChildType = VariableType.GetFieldAtIndex(Child, ChildName, &BitOffset,
                                                  nullptr, nullptr);
    std::cerr << "Child: " << ChildName << ": " << BitOffset << "\n";
    assert(BitOffset % 8 == 0 && "Expecting fields to be byte-aligned");

    MemoryLocation ChildLocation = Variable;
    ChildLocation.Offset += BitOffset / 8;
    auto Result = formatVariable(Builder, Buffer, Size, ChildName, ChildType,
                                 ChildLocation);
    if (!Result)
      return Result;
    std::tie(Buffer, Size) = *Result;
  }
  return callFormatter(Builder, getArrayEndFormatter(M), Buffer, Size);
}

Expected<VariablePrinter::StringSlice> VariablePrinter::formatArray(
    IRBuilder<> &Builder, Value *Buffer, Value *Size, StringRef Name,
    const lldb_private::CompilerType &ElementType,
    const MemoryLocation &Variable, size_t ArraySize, bool Incomplete) {
  assert(!Incomplete && "not implemented");
  auto &M = *Builder.GetInsertBlock()->getModule();
  auto ElementSize = ElementType.GetByteSize(nullptr);
  if (!ElementSize)
    return createStringError(inconvertibleErrorCode(),
                             "Cannot determing byte size of type '%s'\n",
                             ElementType.GetTypeName().GetCString());

  llvm::errs() << "Formatting array of type "
               << ElementType.GetTypeName().GetStringRef() << " with "
               << ArraySize << " elements\n";

  Value *TypeName =
      Builder.CreateGlobalStringPtr(ElementType.GetTypeName().GetStringRef());
  Value *VarName = Builder.CreateGlobalStringPtr(Name);

  std::tie(Buffer, Size) = callFormatter(Builder, getArrayBeginFormatter(M),
                                         Buffer, Size, VarName, TypeName);

  MemoryLocation ElementLocation = Variable;
  for (size_t Element = 0; Element < ArraySize; ++Element) {
    if (Element > 0)
      std::tie(Buffer, Size) =
          callFormatter(Builder, getSepFormatter(M), Buffer, Size);
    // FIXME maybe don't unroll this thing?
    std::string ElementName =
        (Name + "[" + std::to_string(Element) + "]").str();
    auto Result = formatVariable(Builder, Buffer, Size, ElementName,
                                 ElementType, ElementLocation);
    if (!Result)
      return Result;
    std::tie(Buffer, Size) = *Result;
    ElementLocation.Offset += *ElementSize;
  }

  return callFormatter(Builder, getArrayEndFormatter(M), Buffer, Size);
}

Expected<VariablePrinter::StringSlice>
VariablePrinter::formatVariable(IRBuilder<> &Builder, Value *Buffer,
                                Value *Size, StringRef Name,
                                const lldb_private::CompilerType &VariableType,
                                const MemoryLocation &Variable) {
  if (VariableType.IsScalarType() || VariableType.IsPointerType())
    return formatPrimitive(Builder, Buffer, Size, Name, VariableType, Variable);

  lldb_private::CompilerType ElementType;
  size_t ArraySize;
  bool Incomplete;
  if (VariableType.IsArrayType(&ElementType, &ArraySize, &Incomplete))
    return formatArray(Builder, Buffer, Size, Name, ElementType, Variable,
                       ArraySize, Incomplete);

  if (VariableType.IsAggregateType())
    return formatAggregate(Builder, Buffer, Size, Name, VariableType, Variable);

  return createStringError(inconvertibleErrorCode(),
                           "Unhandled type category for type '%s'",
                           Variable.Type.c_str());
}

llvm::Expected<std::unique_ptr<llvm::Module>>
VariablePrinter::generateModule(StringRef Name,
                                const lldb_private::CompilerType &Type,
                                const MemoryLocation &Location) {
  auto M = std::make_unique<llvm::Module>("wasm_eval", MainContext);
  IRBuilder<> Builder(MainContext);

  auto *Entry = createFunction(*M, "wasm_format", Builder.getVoidTy(),
                               {{"OutputBuffer", Builder.getInt8PtrTy()},
                                {"BufferSize", Builder.getInt32Ty()}});
  Builder.SetInsertPoint(BasicBlock::Create(MainContext, "entry", Entry));
  Builder.SetInsertPoint(Builder.CreateRetVoid());
  auto *Buffer = &*Entry->arg_begin();
  auto *Size = &*(Entry->arg_begin() + 1);

  auto Status = formatVariable(Builder, Buffer, Size, Name, Type, Location);
  if (!Status)
    return Status.takeError();

  return M;
}

std::unique_ptr<llvm::Module> VariablePrinter::loadRuntimeModule() {
  SMDiagnostic Err;
  SmallString<128> FormatterModuleFile(LLDB_CDLC_TOOL_DIR);
  sys::path::append(FormatterModuleFile, "formatters.bc");
  auto M = getLazyIRFileModule(FormatterModuleFile, Err, MainContext);
  if (!M)
    Err.print("RuntimeModule", errs());
  return M;
}

class TempFileScope {
  sys::fs::TempFile &TheFile;

public:
  TempFileScope(sys::fs::TempFile &F) : TheFile(F) {}
  ~TempFileScope() {
    auto E = TheFile.discard();
    if (E) {
      errs() << "Failed to delete temporary file " << TheFile.TmpName << ": "
             << E << "\n";
    }
  }
};

static auto getTempFile(StringRef Model) {
  SmallString<128> TFile;
  sys::path::system_temp_directory(true, TFile);
  sys::path::append(TFile, Model);
  return sys::fs::TempFile::create(TFile);
}

std::unique_ptr<MemoryBuffer> VariablePrinter::generateCode(llvm::Module &M) {
  Triple WasmTriple("wasm32-unknown-unknown-wasm");

  if (auto RuntimeModule = loadRuntimeModule()) {
    Linker ModuleLinker(M);
    ModuleLinker.linkInModule(std::move(RuntimeModule));
    // M.dump();
  }

  std::string ErrMsg;
  auto WasmTarget =
      TargetRegistry::lookupTarget(WasmTriple.getTriple(), ErrMsg);

  if (!WasmTarget) {
    errs() << ErrMsg << "\n";
    return {};
  }

  std::unique_ptr<TargetMachine> TargetM(WasmTarget->createTargetMachine(
      WasmTriple.getTriple(), "", "", {}, /*RelocModel=*/None));
  auto ObjFile = getTempFile("wasm_formatter-%%%%%%.o");
  if (!ObjFile) {
    errs() << "Failed to create temporary object file: " << ObjFile.takeError()
           << "\n";
    return {};
  }
  TempFileScope ObjScope(*ObjFile);
  raw_fd_ostream ASMStream(ObjFile->FD, false);

  llvm::legacy::PassManager PM;
  PM.add(createTargetTransformInfoWrapperPass(TargetM->getTargetIRAnalysis()));

  if (TargetM->addPassesToEmitFile(PM, ASMStream, nullptr, CGFT_ObjectFile,
                                   true /* verify */)) {
    errs() << "The target does not support generation of this file type!\n";
    return {};
  }
  PM.run(M);
  ASMStream.flush();

  auto ModuleFile = getTempFile("wasm_formatter-%%%%%%.wasm");
  if (!ModuleFile) {
    errs() << "Failed to create temporary module file: "
           << ModuleFile.takeError() << "\n";
    return {};
  }
  TempFileScope ModuleScope(*ModuleFile);
  const char *LinkArgs[] = {"wasm-ld",
                            ObjFile->TmpName.c_str(),
                            "--export=wasm_format",
                            "--export=__heap_base",
                            "--export=__data_end",
                            "--no-entry",
                            "--allow-undefined",
                            "--import-memory",
                            "-o",
                            ModuleFile->TmpName.c_str()};
  errs() << "Linking: ";
  for (auto *A : LinkArgs)
    errs() << A << " ";
  errs() << "\n";

  if (!lld::wasm::link(LinkArgs, false, llvm::outs(), llvm::errs())) {
    errs() << "Linking failed\n";
    return {};
  }

  auto Data = MemoryBuffer::getFile(ModuleFile->TmpName);
  if (!Data) {
    errs() << "Failed to read temporary module file: "
           << Data.getError().message() << "\n";
    return {};
  }
  errs() << (*Data)->getBufferSize() << "\n";

  return std::move(*Data);
}

#define MAKE_FORMATTER(name, type)                                             \
  {                                                                            \
    name, std::function<FunctionCallee(llvm::Module &)>(                       \
              [](llvm::Module &M) -> FunctionCallee {                          \
                return M.getOrInsertFunction(                                  \
                    "format_" name, Type::getInt32Ty(M.getContext()),          \
                    Type::get##type##PtrTy(M.getContext()),                    \
                    Type::getInt8PtrTy(M.getContext()),                        \
                    Type::getInt8PtrTy(M.getContext()),                        \
                    Type::getInt32Ty(M.getContext()));                         \
              })                                                               \
  }

VariablePrinter::VariablePrinter()
    : PrimitiveFormatters(
          {MAKE_FORMATTER("int64_t", Int64),
           MAKE_FORMATTER("int32_t", Int32),
           MAKE_FORMATTER("int", Int32),
           MAKE_FORMATTER("int8_t", Int8),
           {"const char *", [](llvm::Module &M) -> FunctionCallee {
              return M.getOrInsertFunction(
                  "format_string", Type::getInt32Ty(M.getContext()),
                  Type::getInt8PtrTy(M.getContext())->getPointerTo(),
                  Type::getInt8PtrTy(M.getContext()),
                  Type::getInt8PtrTy(M.getContext()),
                  Type::getInt32Ty(M.getContext()));
            }}}) {
  LLVMInitializeWebAssemblyTarget();
  LLVMInitializeWebAssemblyTargetInfo();
  LLVMInitializeWebAssemblyTargetMC();
  LLVMInitializeWebAssemblyAsmPrinter();
}

} // namespace cdlc
} // namespace lldb
