//===--- CollectMacros.h -----------------------------------------*- C++-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_TOOLS_EXTRA_CLANGD_COLLECTEDMACROS_H
#define LLVM_CLANG_TOOLS_EXTRA_CLANGD_COLLECTEDMACROS_H

#include "Protocol.h"
#include "SourceCode.h"
#include "clang/Basic/IdentifierTable.h"
#include "clang/Lex/PPCallbacks.h"
#include <string>

namespace clang {
namespace clangd {

struct MainFileMacros {
  llvm::StringSet<> Names;
  // Instead of storing SourceLocation, we have to store the token range because
  // SourceManager from preamble is not available when we build the AST.
  std::vector<Range> Ranges;
};

/// Collects macro references (e.g. definitions, expansions) in the main file.
/// It is used to:
///  - collect macros in the preamble section of the main file (in Preamble.cpp)
///  - collect macros after the preamble of the main file (in ParsedAST.cpp)
class CollectMainFileMacros : public PPCallbacks {
public:
  explicit CollectMainFileMacros(const SourceManager &SM,
                                 const LangOptions &LangOpts,
                                 MainFileMacros &Out)
      : SM(SM), LangOpts(LangOpts), Out(Out) {}

  void FileChanged(SourceLocation Loc, FileChangeReason,
                   SrcMgr::CharacteristicKind, FileID) override {
    InMainFile = isInsideMainFile(Loc, SM);
  }

  void MacroDefined(const Token &MacroName, const MacroDirective *MD) override {
    add(MacroName, MD->getMacroInfo());
  }

  void MacroExpands(const Token &MacroName, const MacroDefinition &MD,
                    SourceRange Range, const MacroArgs *Args) override {
    add(MacroName, MD.getMacroInfo());
  }

  void MacroUndefined(const clang::Token &MacroName,
                      const clang::MacroDefinition &MD,
                      const clang::MacroDirective *Undef) override {
    add(MacroName, MD.getMacroInfo());
  }

  void Ifdef(SourceLocation Loc, const Token &MacroName,
             const MacroDefinition &MD) override {
    add(MacroName, MD.getMacroInfo());
  }

  void Ifndef(SourceLocation Loc, const Token &MacroName,
              const MacroDefinition &MD) override {
    add(MacroName, MD.getMacroInfo());
  }

  void Defined(const Token &MacroName, const MacroDefinition &MD,
               SourceRange Range) override {
    add(MacroName, MD.getMacroInfo());
  }

private:
  void add(const Token &MacroNameTok, const MacroInfo *MI) {
    if (!InMainFile)
      return;
    auto Loc = MacroNameTok.getLocation();
    if (Loc.isMacroID())
      return;

    if (auto Range = getTokenRange(SM, LangOpts, Loc)) {
      Out.Names.insert(MacroNameTok.getIdentifierInfo()->getName());
      Out.Ranges.push_back(*Range);
    }
  }
  const SourceManager &SM;
  const LangOptions &LangOpts;
  bool InMainFile = true;
  MainFileMacros &Out;
};

} // namespace clangd
} // namespace clang

#endif // LLVM_CLANG_TOOLS_EXTRA_CLANGD_COLLECTEDMACROS_H
