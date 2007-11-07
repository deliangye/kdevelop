/* This file is part of KDevelop
    Copyright 2007 David Nolden <david.nolden.kdevelop@art-master.de>

   This library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Library General Public
   License version 2 as published by the Free Software Foundation.

   This library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Library General Public License for more details.

   You should have received a copy of the GNU Library General Public License
   along with this library; see the file COPYING.LIB.  If not, write to
   the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
   Boston, MA 02110-1301, USA.
*/

#include "expressionparser.h"

#include <duchain.h>
#include "cppduchain/usebuilder.h"
#include "cppduchain/declarationbuilder.h"
#include "cppduchain/dumpchain.h"
#include "cppduchain/dumptypes.h"
#include <declaration.h>

#include "ducontext.h"
#include "ast.h"
#include "parsesession.h"
#include "parser.h"
#include "control.h"
#include "duchainlock.h"
//#include "typerepository.h"
#include <identifier.h>
#include "expressionvisitor.h"


namespace Cpp {
using namespace KDevelop;

ExpressionParser::ExpressionParser( bool strict, bool debug ) : m_strict(strict), m_debug(debug) {
}

ExpressionEvaluationResult ExpressionParser::evaluateType( const QByteArray& unit, DUContextPointer context, bool statement ) {

  if( m_debug )
    kDebug(9007) << "==== .Evaluating ..:" << endl << unit;

  ParseSession* session = new ParseSession();

  Control control;
  DumpChain dumper;

  Parser parser(&control);

  AST* ast = 0;

  DUContext::ContextType type;
  {
    DUChainReadLocker lock(DUChain::lock());
    if( !context )
      return ExpressionEvaluationResult();
    type = context->type();
  }

  if( statement ) {
      session->setContentsAndGenerateLocationTable('{' + unit + ";}");
      ast = parser.parseStatement(session);
  } else {
      session->setContentsAndGenerateLocationTable(unit);
      ast = parser.parse(session);
      ((TranslationUnitAST*)ast)->session = session;
  }

  if (m_debug) {
    kDebug(9007) << "===== AST:";
    dumper.dump(ast, session);
  }

  ast->ducontext = context.data();

  ///@todo think how useful it is to compute contexts and uses here. The main thing we need is the AST.
  /*
  static int testNumber = 0; //@todo what this url for?
  KUrl url(QString("file:///internal/evaluate_%1").arg(testNumber++));
  kDebug(9007) << "url:" << url;

  DeclarationBuilder definitionBuilder(session);
  DUContext* top = definitionBuilder.buildSubDeclarations(url, ast, context);

  UseBuilder useBuilder(session);
  useBuilder.buildUses(ast);

  if (m_debug) {
    kDebug(9007) << "===== DUChain:";

    DUChainReadLocker lock(DUChain::lock());
    dumper.dump(top, false);
  }

  if (m_debug) {
    kDebug(9007) << "===== Types:";
    DumpTypes dt;
    DUChainReadLocker lock(DUChain::lock());
    foreach (const AbstractType::Ptr& type, definitionBuilder.topTypes())
      dt.dump(type.data());
  }

  if (m_debug)
    kDebug(9007) << "===== Finished evaluation.";
  */
  ExpressionEvaluationResult ret = evaluateType( ast, session );

  delete session;

  /*
  {
    DUChainReadLocker lock(DUChain::lock());
    delete top;
  }*/

  return ret;
}

ExpressionEvaluationResult ExpressionParser::evaluateType( AST* ast, ParseSession* session) {
  ExpressionEvaluationResult ret;
  ExpressionVisitor v(session, m_strict);
  v.parse( ast );
  ret.type = v.lastType();
  ret.instance = v.lastInstance();
  ret.allDeclarations = v.lastDeclarations();
  return ret;
}

}
