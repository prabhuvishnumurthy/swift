//===--- ParsePattern.cpp - Swift Language Parser for Patterns ------------===//
//
// This source file is part of the Swift.org open source project
//
// Copyright (c) 2014 - 2015 Apple Inc. and the Swift project authors
// Licensed under Apache License v2.0 with Runtime Library Exception
//
// See http://swift.org/LICENSE.txt for license information
// See http://swift.org/CONTRIBUTORS.txt for the list of Swift project authors
//
//===----------------------------------------------------------------------===//
//
// Pattern Parsing and AST Building
//
//===----------------------------------------------------------------------===//

#include "Parser.h"
using namespace swift;

/// Check that the given pattern is fully-typed.
static bool checkFullyTyped(Parser &P, Pattern *pattern) {
  switch (pattern->getKind()) {
  // Any type with an explicit annotation is okay.
  case PatternKind::Typed:
    return false;

  // Paren types depend on their parenthesized pattern.
  case PatternKind::Paren: {
    Pattern *sub = cast<ParenPattern>(pattern)->getSubPattern();
    if (checkFullyTyped(P, sub)) return true;
    pattern->setType(sub->getType());
    return false;
  }

  // Tuple types can be built up from their components.
  case PatternKind::Tuple: {
    TuplePattern *tuple = cast<TuplePattern>(pattern);
    SmallVector<TupleTypeElt, 8> typeElts;
    typeElts.reserve(tuple->getNumFields());
    for (const TuplePatternElt &elt : tuple->getFields()) {
      Pattern *subpattern = elt.getPattern();
      if (checkFullyTyped(P, subpattern))
        return true;
      typeElts.push_back(TupleTypeElt(subpattern->getType(),
                                      subpattern->getBoundName(),
                                      elt.getInit()));
    }
    tuple->setType(TupleType::get(typeElts, P.Context));
    return false;
  }

  // Everything else is uninferrable.
  case PatternKind::Named:
  case PatternKind::Any:
    P.diagnose(pattern->getLoc(), diag::untyped_pattern_in_function_signature)
      << pattern->getSourceRange();
    return true;
  }
  llvm_unreachable("bad pattern kind");
}

/// Parse a function definition signature.
bool Parser::parseFunctionSignature(SmallVectorImpl<Pattern*> &params,
                                    Type &type) {
  // Parse curried function argument clauses as long as we can.
  bool hasSemaError = false;
  do {
    ParseResult<Pattern> pattern = parsePatternTuple();
    if (pattern.isParseError()) {
      return true;
    } else if (pattern.isSemaError()) {
      hasSemaError = true;
    } else {
      params.push_back(pattern.get());
    }
  } while (Tok.is(tok::l_paren) || Tok.is(tok::l_paren_space));

  // If there's a trailing arrow, parse the rest as the result type.
  if (consumeIf(tok::arrow)) {
    if (parseType(type))
      return true;

  // Otherwise, we implicitly return ().
  } else {
    type = TupleType::getEmpty(Context);
  }

  // Now build up the function type.  We require all function
  // signatures to be fully-typed: that is, all top-down paths to a
  // leaf pattern must pass through a TypedPattern.
  for (unsigned i = params.size(); i != 0; --i) {
    Pattern *param = params[i - 1];

    Type paramType;
    if (checkFullyTyped(*this, param)) {
      // Recover by ignoring everything.
      paramType = TupleType::getEmpty(Context);
    } else {
      paramType = param->getType();
    }
    type = FunctionType::get(paramType, type, Context);
  }

  return false;
}

/// Parse a pattern.
///   pattern ::= pattern-atom
///   pattern ::= pattern-atom ':' type
ParseResult<Pattern> Parser::parsePattern() {
  // First, parse the pattern atom.
  ParseResult<Pattern> pattern = parsePatternAtom();
  if (pattern.isParseError()) return true;

  // Now parse an optional type annotation.
  if (consumeIf(tok::colon)) {
    Type type;
    if (parseType(type))
      return true;

    if (!pattern.isSemaError())
      pattern = new (Context) TypedPattern(pattern.get(), type);
  }

  return pattern;
}

/// Parse a pattern "atom", meaning the part that precedes the
/// optional type annotation.
///
///   pattern-atom ::= identifier
///   pattern-atom ::= pattern-tuple
ParseResult<Pattern> Parser::parsePatternAtom() {
  switch (Tok.getKind()) {
  case tok::l_paren:
  case tok::l_paren_space:
    return parsePatternTuple();

  case tok::identifier: {
    SourceLoc loc = Tok.getLoc();
    StringRef text = Tok.getText();
    consumeToken(tok::identifier);

    // '_' is a special case which means 'ignore this'.
    if (text == "_") {
      return new (Context) AnyPattern(loc);
    } else {
      Identifier ident = Context.getIdentifier(text);
      VarDecl *var = new (Context) VarDecl(loc, ident, Type(), nullptr,
                                           CurDeclContext);
      return new (Context) NamedPattern(var);
    }
  }

  default:
    diagnose(Tok, diag::expected_pattern);
    return true;
  }
}

/// Parse a tuple pattern.
///
///   pattern-tuple:
////    '(' pattern-tuple-body? ')'
///   pattern-tuple-body:
///     pattern-tuple-element (',' pattern-tuple-body)*
///   pattern-tuple-element:
///     pattern ('=' expr)?
ParseResult<Pattern> Parser::parsePatternTuple() {
  assert(Tok.is(tok::l_paren) || Tok.is(tok::l_paren_space));

  // We're looking at the left parenthesis; consume it.
  SourceLoc lp = consumeToken();

  // Parse all the elements.
  SmallVector<TuplePatternElt, 8> elts;
  bool hasSemaError = false;
  if (Tok.isNot(tok::r_paren)) {
    do {
      ParseResult<Pattern> pattern = parsePattern();
      Expr *init = nullptr;

      if (pattern.isParseError()) {
        skipUntil(tok::r_paren);
        return true;
      } else if (consumeIf(tok::equal)) {
        ParseResult<Expr> initR = parseExpr(diag::expected_initializer_expr);
        if (initR.isParseError()) {
          skipUntil(tok::r_paren);
          return true;
        } else if (initR.isSemaError()) {
          hasSemaError = true;
        } else {
          init = initR.get();
        }
      }

      if (pattern.isSemaError()) {
        hasSemaError = true;
      } else {
        elts.push_back(TuplePatternElt(pattern.get(), init));
      }
    } while (consumeIf(tok::comma));

    if (Tok.isNot(tok::r_paren)) {
      diagnose(Tok, diag::expected_rparen_tuple_pattern_list);
      skipUntil(tok::r_paren);
      return true;
    }
  }

  // Consume the right parenthesis.
  SourceLoc rp = consumeToken(tok::r_paren);

  if (hasSemaError)
    return ParseResult<Pattern>::getSemaError();

  // A pattern which wraps a single anonymous pattern is not a tuple.
  if (elts.size() == 1 &&
      elts[0].getInit() == nullptr &&
      elts[0].getPattern()->getBoundName().empty())
    return new (Context) ParenPattern(lp, elts[0].getPattern(), rp);

  return TuplePattern::create(Context, lp, elts, rp);
}    
