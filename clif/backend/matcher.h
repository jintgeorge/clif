/*
 * Copyright 2017 Google Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
// Takes a clif proto with function and type decls and matches them to the
// appropriate C++ functions and types.
//
// This header is internal to clif. Don't use it other places.
//
// We are dealing with two different kinds of abstract syntax trees
// internally here:
//
// 1. Clif protobufs--clif::AST
// 2. Clang TranslationUnitASTs--clif::TranslationUnitASTs
//
// Keeping them straight in your mind can be tricky.


#ifndef CLIF_BACKEND_MATCHER_H_
#define CLIF_BACKEND_MATCHER_H_

#include <list>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "clif/backend/ast.h"
#include "clif/backend/code_builder.h"
#include "clif/protos/ast.pb.h"
#include "gtest/gtest.h"  // Defines FRIEND_TEST.


namespace clif {

using protos::AST;
using protos::ClassDecl;
using protos::ConstDecl;
using protos::Decl;
using protos::EnumDecl;
using protos::ForwardDecl;
using protos::FuncDecl;
using protos::Type;
using protos::VarDecl;

enum ClifErrorCode {
  kOK = 0,
  kNotFound,
  // Matcher found more than one identifier with the name.
  kMultipleMatches,
  // User said "import foo from bar.h", but foo was in a different file.
  kNotInImportFile,
  // Clif and C++ disagree over whether Foo is a class or a function,
  // (or something else). This is subtly different from a C++ type
  // mismatch, where you might be assigning a class of one type to a
  // class of another type.
  kTypeMismatch,
  // Signature of function can't be matched. Be sure to add
  // messages describing which parts.
  kReturnValueMismatch,
  // A parameter (either input or output) to a function didn't match.
  kParameterMismatch,
  // Clif thinks a symbol is const, but C++ doesn't.
  kConstVarError,
  // C++ is missing an enumerator clif expects is present.
  kMissingEnumerator,
  // Input parameter is a pointer or reference and not const-qualified.
  kNonConstParameterType,
  // Clif variables can't be constant.
  kConstVariable,
  // Output parameter can't be output.
  kNonPointerReturnType,
  // Clif requires this type to be a pointer.
  kNonPointerType,
  // Output parameter can't be written.
  kConstReturnType,
  // Types can't be assigned to each other.
  kIncompatibleTypes,
  // Too many parameters on one side or the other.
  kParameterCountsDiffer,
  // Matching a class with multiple inheritance.
  kMultipleInheritance,
  // C++ entity was only forward declared, but clif wants matching of
  // its members.
  kNoDefinitionAvailable,
  // Clif thinks this type is std::function, but it isn't.
  kNotCallable,
  // Found a template with the right name, but couldn't specialize it.
  kUnspecializableTemplate,
  // C++ constructor not found. The horribleness around template and
  // class names makes it useful to have it's own message.
  kConstructorNotFound,
  // Clif uses @class method on C++ non-static function.
  kClassMethod,
  // C++ static function matched a clif non-class method.
  kCppStaticMethod,
  // Globally-declared function matches a non-static class method.
  kNonStaticClassGlobalFunctionDecl,
};

static const char kVariableNameForError[] = "variable";
static const char kConstNameForError[] = "constant";
static const char kClassNameForError[] = "class";
static const char kTemplateNameForError[] = "template";
static const char kEnumNameForError[] = "enum";
static const char kFunctionNameForError[] = "function";

// Flags which decide how a matched type's C++ name will be reported in
// the output buffer.
enum TypeMatchFlags : unsigned int {
  // Report the exact C++ type.
  TMF_EXACT_TYPE = 0,

  // If the CLIF type is of a derived class type of the C++ type, then
  // the derived class type will be reported.
  TMF_DERIVED_CLASS_TYPE = 1 << 0,

  // If the C++ type is of a pointer type, then the pointee type will be
  // reported.
  TMF_POINTEE_TYPE = 1 << 1,

  // If the C++ type is a reference type and the clif type requires a
  // conversion to that type, report the clif type, not the C++ type.
  TMF_UNCONVERTED_REF_TYPE = 1 << 2,

  // If the C++ type is a constant type, remove the const. This is useful
  // to distinguish between input parameters and return values:
  //
  // const int* Foo(const int*x);
  //
  // For this function, the return type should be declared as constant,
  // but the input type should not.
  TMF_REMOVE_CONST_POINTER_TYPE = 1 << 3,
};

class ClifError;
class ClifMatcherTest;

class ClifMatcher {
  friend class ClifMatcherTest;
 public:
  ClifMatcher() = default;

  // Main entry point and one-stop-shop for doing all the work. Does the
  // following in order:
  // 1. Copies the input proto |clif_ast| into |modified_clif_ast|.
  // 2. Looks up the decls listed in the input proto in the respective
  //    files specified in it. While doing this, the relevant fields
  //    in |modified_clif_ast| are filled/adjusted.
  // Returns true only if all decls in |clif_ast| were successfully matched.
  // Errors will be filled in the relevant error fields of |modified_clif_ast|.
  bool CompileMatchAndSet(const std::vector<std::string>& compiler_args,
                          const std::string& input_file_name,
                          const AST& clif_ast,
                          AST* modified_clif_ast);

  // Entry point that assumes you called RunCompiler with your own
  // options. Ignores all the cpp_files and usertype_includes included
  // in the ast. Directly modifies ast, but otherwise follows the
  // semantics of CompileMatchAndSet.
  bool MatchAndSetAST(AST* ast);

  // Compile the given code with the given args and file.
  bool RunCompiler(const std::string& code,
                   const std::vector<std::string>& args,
                   const std::string& input_file_name);

  const std::string GetDeclCppName(const Decl& decl) const;

 private:
  // Helper class to map clif::clif_type_XX to the resulting qualtypes.
  struct ClifQualTypeDecl {
    clang::QualType qual_type;
    clang::SourceLocation loc;
  };

  // Type table data structures.
  typedef std::pair<std::string, clang::QualType>  ClangTypeInfo;
  typedef std::vector<ClangTypeInfo> TypeInfoList;
  typedef std::unordered_map<std::string, TypeInfoList> ClifToClangTypeMap;
  typedef std::unordered_map<std::string, ClifQualTypeDecl> ClifQualTypes;

  // Calls MatchAndSetXXXX for each decl in the list.  Returns the
  // number of unmatched decls.
  int MatchAndSetDecls(DeclList* decls);

  // The MatchAndSetXXX functions fill in the appropriate fields and
  // values by matching decls and resolving overloads and types and
  // store output (including error messages) in various fields of the
  // proto.  Return true on successful match, false on failure.
  bool MatchAndSetOneDecl(Decl* clif_decl);

  bool MatchAndSetClassName(ForwardDecl* clif_decl) const;

  bool MatchAndSetClass(ClassDecl* clif_decl);

  // Helper for MatchAndSetClass.
  bool CalculateBaseClasses(const clang::CXXRecordDecl* clang_decl,
                            ClassDecl* clif_decl) const;

  bool MatchAndSetEnum(EnumDecl* clif_decl);

  bool MatchAndSetVar(VarDecl* clif_decl);

  bool MatchAndSetConst(ConstDecl* clif_decl);

  bool MatchAndSetFunc(FuncDecl* clif_decl);

  // Match and set an operator following clif lookup rules.
  bool MatchAndSetOperator(FuncDecl* operator_decl);

  // Helper for MatchAndSetOperator that finds operator overloads in
  // particular contexts.
  bool MatchAndSetOperatorInContext(
      clang::DeclContext* context,
      FuncDecl* operator_decl);

  bool MatchAndSetConstructor(
      clang::CXXRecordDecl* class_decl,
      const clang::SourceLocation& loc,
      FuncDecl* func_decl);

  // Returns the matched function decl if one is found, or nullptr if
  // there is no match.
  const clang::FunctionDecl* MatchAndSetFuncFromCandidates(
      const ClifLookupResult& candidates,
      FuncDecl* func_decl);

  ClifErrorCode MatchAndSetSignatures(
      const clang::FunctionProtoType* clang_type,
      int min_req_args,
      FuncDecl* func_decl,
      std::string* message);

  // Helper functions for MatchAndSetSignatures
  ClifErrorCode MatchAndSetReturnValue(
      const clang::FunctionProtoType* clang_type,
      FuncDecl* func_decl,
      bool* consumed_return_value,
      std::string* message);

  ClifErrorCode MatchAndSetOutputParamType(const clang::QualType& clang_type,
                                           Type* clif_type);

  ClifErrorCode MatchAndSetInputParamType(clang::QualType clang_type,
                                          Type* clif_type);

  ClifErrorCode MatchAndSetVarHelper(bool check_constant,
                                     protos::Name* name,
                                     protos::Type* type);

  ClifErrorCode HandleEnumConstant(bool check_constant,
                                   clang::EnumConstantDecl* enum_decl,
                                   protos::Name* name,
                                   protos::Type* type);

  // Return if the two decls agree on if the given function is static.
  ClifErrorCode MatchFunctionStatic(const clang::FunctionDecl* clang_decl,
                                    const FuncDecl& func_decl);

  bool CheckConstant(clang::QualType type) const;

  // Primary entry-point when matching types. Does a small amount of
  // setup and then calls into the primary matching mechanism, which
  // does recursive handling of complicated types.
  ClifErrorCode MatchAndSetTypeTop(const clang::QualType& reffed_type,
                                   Type* clif_type,
                                   unsigned int flags = TMF_EXACT_TYPE);

  // Dispatcher to handle both the top-level type and any children.
  ClifErrorCode MatchAndSetType(const clang::QualType& reffed_type,
                                Type* clif_type,
                                unsigned int flags = TMF_EXACT_TYPE);

  // Handle C++ return type special cases.
  ClifErrorCode MatchAndSetReturnType(const clang::QualType& clang_ret,
                                      Type* clif_type_proto);

  // Helper for MatchAndSetType which handles the primary fields of
  // a clif Type proto. Other functions handle the children.
  ClifErrorCode MatchSingleType(const clang::QualType& reffed_type,
                                const Type* clif_type);

  // Adds to a list of types so that the user can see exactly where
  // the failure occurred. These types may not actually be recorded,
  // as in the case of a function with overloads. GetParallelTypeNames
  // does the actual reporting.
  ClifErrorCode RecordIncompatibleTypes(const clang::QualType& clang_type,
                                        const Type& clif_type);

  // Special case handling for smart pointers.
  ClifErrorCode MatchAndSetStdSmartPtr(const clang::QualType& template_type,
                                       protos::Type* clif_type,
                                       unsigned int flags);

  // When reporting a match between a clang_type and a clif_qual_type,
  // the exact type to report depends on a variety of factors, such as
  // whether this type is an input parameter or a return value.
  // Return the type to report as described in flags.
  clang::QualType HandleTypeMatchFlags(const clang::QualType& clang_type,
                                       const clang::QualType& clif_qual_type,
                                       unsigned int flags);

  void SetUnqualifiedCppType(const clang::QualType& clang_type,
                             Type* clif_type);

  // Helper for MatchAndSetType which handles the complicated
  // clif-callable children of Type.
  ClifErrorCode MatchAndSetCallable(const clang::QualType& callable_type,
                                    FuncDecl* callable);

  // Helper for SetClifCppType which handles clif-composed types.
  ClifErrorCode MatchAndSetContainer(const clang::QualType& reffed_type,
                                     protos::Type* container,
                                     unsigned int flags);

  // Helper to work with function templates.
  const clang::FunctionDecl* SpecializeFunctionTemplate(
      clang::FunctionTemplateDecl* template_decl,
      FuncDecl* func_decl) const;

  // Add the class type as the first parameter to a function.
  void AdjustForNonClassMethods(protos::FuncDecl* clif_func_decl) const;

  // Return named_decl dynamically cast to the given ClangDecl type.
  // If this cannot be done, generate an error and store it in
  // named_decl's error field, then return nullptr.
  template<class ClangDeclType> ClangDeclType* TypecheckLookupResult(
      clang::NamedDecl* named_decl,
      const std::string& clif_identifier,
      const std::string& clif_type) const;

  // Return named_decl dynamically cast to the given ClangDecl type,
  // or nullptr if that is not possible.
  template<class ClangDeclType>
  ClangDeclType* CheckDeclType(clang::NamedDecl* named_decl) const;

  // Report a typecheck error and store it in named_decl's error
  // field.
  template<class ClangDeclType>
  void ReportTypecheckError(
      clang::NamedDecl* named_decl,
      const std::string& clif_identifier,
      const std::string& clif_type) const;

  // Count the decls and report an error for count != 1.
  ClifErrorCode CheckForLookupError(const ClifLookupResult& decls) const;

  // Display the types recorded by "RecordIncompatibleTypes, by
  // formattting a string displaying the types in the canonical,
  // "Here's what Clif says, and it doesn't match what C++ says" way.
  std::string GetParallelTypeNames() const;

  // Did the named_decl come from the file that the clif_decl
  // requests?  If not, then error will contain the details of the
  // failure. Caller is responsible to display the error if needed.
  bool ImportedFromCorrectFile(const clang::NamedDecl& named_decl,
                               ClifError* error) const;

  // Builds an internal convenience type table.
  void BuildTypeTable();

  // CLif forbids many normal C++ type promotions. Return true if
  // promoting from_type to to_type is a legal Clif type promotion.
  bool IsValidClifTypePromotion(clang::QualType from_type,
                                clang::QualType to_type) const;

  // Return if a variable of C++ type_a (which was declared by clif)
  // can be assigned to a C++ variable of type_b (which was declared
  // somewhere in the source code). This has all the subtlety that one
  // would expect from C++: move constructors, copy constructors,
  // type-promotion, etc.  The only exception that it happens at the
  // translation unit's top-level scope, rather than any deep
  // namespace. It's likely type-dependent lookup does not work
  // exactly right.  SourceLoc is so that if things go really badly,
  // clang will report the error as originating from that location.
  bool AreAssignableTypes(const clang::QualType type_a,
                          const clang::SourceLocation& loc,
                          clang::QualType type_b) const;

  // Sets various fields of a type-related clif_proto appropriately.
  template<class T>
  void SetTypeProperties(clang::QualType clang_type, T* clif_proto) const;

  std::string GetQualTypeClifName(clang::QualType qual_type) const;

  void SetCppTypeName(const std::string& name, protos::Type* type) const {
    type->set_cpp_type(name);
  }

  Decl* CurrentDecl() const { return decl_stack_.back(); }

  const std::string CurrentCppFile() const {
    for (auto decl = decl_stack_.rbegin(); decl != decl_stack_.rend(); ++decl) {
      if ((*decl)->has_cpp_file()) {
        return (*decl)->cpp_file();
      }
    }
    return "";
  }

  const ClassDecl* EnclosingClifClass() const {
    for (auto decl = decl_stack_.rbegin(); decl != decl_stack_.rend(); ++decl) {
      if ((*decl)->has_class_()) {
        return &((*decl)->class_());
      }
    }
    return nullptr;
  }

  std::unique_ptr<TranslationUnitAST> ast_;

  const TypeInfoList empty_;

  std::vector<protos::Decl*> decl_stack_;
  // Stack to report various fragments of mismatched type names. Clang
  // name first, clif name second.
  std::vector<std::pair<std::string, std::string>> type_mismatch_stack_;
  ClifQualTypes clif_qual_types_;
  CodeBuilder builder_;

  FRIEND_TEST(ClifMatcherTest, TestMatchAndSetOneDecl);
  FRIEND_TEST(ClifMatcherTest, TestFuncFieldsFilled);
  FRIEND_TEST(ClifMatcherTest, TestClassFieldsFilled);
  FRIEND_TEST(ClifMatcherTest, TestContainerTypes);
  FRIEND_TEST(ClifMatcherTest, TestUsingDecls);
  FRIEND_TEST(ClifMatcherTest, BuildCode);
};

class ClifError {
 public:
  ClifError() = delete;

  explicit ClifError(const ClifMatcher& matcher, ClifErrorCode code)
      : matcher_(matcher), code_(code) {
    // Not found errors should always include a message describing the
    // lookup location, and therefore should use the other
    // constructor.
    assert(code_ != kNotFound && code != kConstructorNotFound);
  }

  ClifError(const ClifMatcher& matcher,
            ClifErrorCode code,
            const std::string& message) : matcher_(matcher), code_(code) {
    AddMessage(message);
  }

  void SetCode(ClifErrorCode code) { code_ = code; }

  ClifErrorCode GetCode() const { return code_; }

  void AddMessage(const std::string& message);

  void AddClangDeclAndLocation(const TranslationUnitAST* ast,
                               const clang::NamedDecl* decl);

  std::string Report(Decl* clif_decl);

 private:
  const ClifMatcher& matcher_;
  ClifErrorCode code_;
  std::vector<std::string> messages_;
};

}  // namespace clif

#endif  // CLIF_BACKEND_MATCHER_H_
