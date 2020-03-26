#include "Plugins/Language/CPlusPlus/CPlusPlusLanguage.h"
#include "Plugins/ObjectFile/ELF/ObjectFileELF.h"
#include "Plugins/ObjectFile/wasm/ObjectFileWasm.h"
#include "Plugins/SymbolFile/DWARF/SymbolFileDWARF.h"
#include "Plugins/SymbolVendor/wasm/SymbolVendorWasm.h"
#include "Protocol.h"
#include "cdlc.pb.h"
#include "lldb/Host/FileSystem.h"
#include "lldb/lldb-private-interfaces.h"
#include "lldb/lldb-types.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Path.h"
#include "gmock/gmock-generated-matchers.h"
#include "gtest/gtest.h"

#include "Modules.h"
#include "Protocol.h"
#include "Util.h"
#include "lldb-cdlc-test-config.h"

static lldb::cdlc::DefaultPluginsContext C;

static llvm::SmallString<32> makeFile(llvm::StringRef FileName,
                                      bool Absolute = false) {
  llvm::SmallString<32> File;
  if (Absolute)
    File = "file://" LLDB_CDLC_TEST_INPUTS_DIRECTORY;
  llvm::sys::path::append(File, FileName);
  return File;
}

struct LLDBLanguageComponentTest : public ::testing::Test {
  lldb::cdlc::ModuleCache Cache;

  const lldb::cdlc::WasmModule *getModule(llvm::StringRef ModuleName) {
    return Cache.getModuleFromUrl(ModuleName, makeFile(ModuleName, true));
  }
};

TEST_F(LLDBLanguageComponentTest, AddScript) {
  EXPECT_TRUE(getModule("hello.wasm")->valid());
}

TEST_F(LLDBLanguageComponentTest, SourceScripts) {
  EXPECT_EQ(getModule("hello.wasm")->getSourceScripts().size(), 2u);
}

TEST_F(LLDBLanguageComponentTest, HelloAddScript) {
  auto *M = getModule("hello.wasm");
  EXPECT_TRUE(M->valid());
  auto Scripts = M->getSourceScripts();
  llvm::SmallVector<llvm::StringRef, 2> Filenames;
  EXPECT_EQ(Scripts.size(), 2u);
  for (auto &S : Scripts)
    Filenames.push_back(S);
  EXPECT_THAT(Filenames, testing::UnorderedElementsAre("hello.c", "printf.h"));
}

TEST_F(LLDBLanguageComponentTest, HelloSourceToRawLocation) {
  getModule("hello.wasm");
  lldb::cdlc::SourceLocation SourceLocation(makeFile("hello.c"), 4, 0);

  const llvm::SmallVector<const lldb::cdlc::WasmModule *, 1> &Modules =
      Cache.findModulesContainingSourceScript(SourceLocation.File);
  EXPECT_EQ(Modules.size(), 1u);
  if (Modules.empty())
    return;

  const llvm::SmallVectorImpl<lldb::addr_t> &RawLocations =
      Modules.front()->getOffsetFromSourceLocation(SourceLocation);
  EXPECT_EQ(RawLocations.size(), 1u);
  EXPECT_THAT(RawLocations, testing::ElementsAre(lldb::addr_t(0xEE - 0x7C)));
}

TEST_F(LLDBLanguageComponentTest, HelloRawToSourceLocation) {
  auto *M = getModule("hello.wasm");
  auto Loc = M->getSourceLocationFromOffset(0xEE - 0x7C);
  EXPECT_EQ(Loc.size(), 1u);
  if (Loc.empty())
    return;
  EXPECT_EQ(Loc.front().File, "hello.c");
  EXPECT_EQ(Loc.front().Column, 3u);
  EXPECT_EQ(Loc.front().Line, 4u);
}

TEST_F(LLDBLanguageComponentTest, InlineRawToSourceLocation) {
  auto *M = getModule("inline.wasm");
  {
    auto Loc = M->getSourceLocationFromOffset(0x167 - 0xFF);
    EXPECT_EQ(Loc.size(), 1u);
    if (Loc.empty())
      return;
    EXPECT_EQ(Loc.front().File, "inline.cc");
    EXPECT_EQ(Loc.front().Column, 7u);
    EXPECT_EQ(Loc.front().Line, 4u);
  }
  {
    auto Loc = M->getSourceLocationFromOffset(0x19F - 0xFF);
    EXPECT_EQ(Loc.size(), 1u);
    if (Loc.empty())
      return;
    EXPECT_EQ(Loc.front().File, "inline.cc");
    EXPECT_EQ(Loc.front().Column, 7u);
    EXPECT_EQ(Loc.front().Line, 4u);
  }
  {
    auto Loc = M->getSourceLocationFromOffset(0x1BB - 0xFF);
    EXPECT_EQ(Loc.size(), 1u);
    if (Loc.empty())
      return;
    EXPECT_EQ(Loc.front().File, "inline.cc");
    EXPECT_EQ(Loc.front().Column, 7u);
    EXPECT_EQ(Loc.front().Line, 10u);
  }
  {
    auto Loc = M->getSourceLocationFromOffset(0x1DC - 0xFF);
    EXPECT_EQ(Loc.size(), 1u);
    if (Loc.empty())
      return;
    EXPECT_EQ(Loc.front().File, "inline.cc");
    EXPECT_EQ(Loc.front().Column, 3u);
    EXPECT_EQ(Loc.front().Line, 16u);
  }  
}

TEST_F(LLDBLanguageComponentTest, AddScriptMissingScript) {
  const lldb::cdlc::WasmModule *M = getModule("@InvalidPath");
  EXPECT_FALSE(M->valid());
}

TEST_F(LLDBLanguageComponentTest, GlobalVariable) {
  auto Variables = getModule("global.wasm")->getVariablesInScope(0x10);
  llvm::SmallVector<llvm::StringRef, 1> Names;
  for (auto &V : Variables)
    Names.push_back(V.Name);
  EXPECT_THAT(Names, testing::UnorderedElementsAre("I"));

  auto Snippet = getModule("global.wasm")->getVariableFormatScript("I", 0x10);
  EXPECT_TRUE(!!Snippet);
  EXPECT_FALSE(Snippet->empty());
}

TEST_F(LLDBLanguageComponentTest, ClassStaticVariable) {
  auto Variables = getModule("classstatic.wasm")->getVariablesInScope(0x10);
  llvm::SmallVector<llvm::StringRef, 1> Names;
  for (auto &V : Variables)
    Names.push_back(V.Name);
  EXPECT_THAT(Names, testing::UnorderedElementsAre("MyClass::I"));

  auto Snippet = getModule("classstatic.wasm")->getVariableFormatScript("I", 0x10);
  EXPECT_TRUE(!!Snippet);
  EXPECT_FALSE(Snippet->empty());
}

TEST_F(LLDBLanguageComponentTest, InlineLocalVariable) {
  auto *M = getModule("inline.wasm");
  {
    const int location = 0x167 - 0xFF;
    auto Variables = M->getVariablesInScope(location);
    llvm::SmallVector<llvm::StringRef, 1> Names;
    for (auto &V : Variables)
      Names.push_back(V.Name);
    EXPECT_THAT(Names, testing::UnorderedElementsAre("x", "result", "x", "y", "dsq", "I"));
  }
  {
    const int location = 0x19F - 0xFF;
    auto Variables = M->getVariablesInScope(location);
    llvm::SmallVector<llvm::StringRef, 1> Names;
    for (auto &V : Variables)
      Names.push_back(V.Name);
    EXPECT_THAT(Names, testing::UnorderedElementsAre("x", "result", "x", "y", "dsq", "I"));
  }
  {
    const int location = 0x1BB - 0xFF;
    auto Variables = M->getVariablesInScope(location);
    llvm::SmallVector<llvm::StringRef, 1> Names;
    for (auto &V : Variables)
      Names.push_back(V.Name);
    EXPECT_THAT(Names, testing::UnorderedElementsAre("x", "y", "dsq", "I"));
  }
  {
    const int location = 0x1DC - 0xFF;
    auto Variables = M->getVariablesInScope(location);
    llvm::SmallVector<llvm::StringRef, 1> Names;
    for (auto &V : Variables)
      Names.push_back(V.Name);
    EXPECT_THAT(Names, testing::UnorderedElementsAre("I"));
  }
}

TEST_F(LLDBLanguageComponentTest, Strings) {
  auto Variables = getModule("string.wasm")->getVariablesInScope(0x11);
  llvm::SmallVector<llvm::StringRef, 1> Names;
  for (auto &V : Variables) {
    Names.push_back(V.Name);
    llvm::errs() << V.Type << "\n";
  }
  EXPECT_THAT(Names, testing::UnorderedElementsAre("String"));
  auto Snippet =
      getModule("string.wasm")->getVariableFormatScript("String", 0x11);
  EXPECT_TRUE(!!Snippet);
  EXPECT_FALSE(Snippet->empty());
}

TEST_F(LLDBLanguageComponentTest, Arrays) {
  auto Variables = getModule("array.wasm")->getVariablesInScope(0x104 - 0x7D);
  llvm::SmallVector<std::pair<llvm::StringRef, llvm::StringRef>, 1> Names;
  EXPECT_EQ(Variables.size(), 1u);
  if (Variables.size() > 0) {
    EXPECT_EQ(Variables.front().Name, "A");
    EXPECT_EQ(Variables.front().Type, "int [4]");
  }

  auto Snippet =
      getModule("array.wasm")->getVariableFormatScript("A", 0x104 - 0x7D);
  EXPECT_TRUE(!!Snippet) << Snippet.takeError();
  EXPECT_FALSE(Snippet->empty());
}
