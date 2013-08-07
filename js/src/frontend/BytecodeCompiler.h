/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 4 -*-
 * vim: set ts=8 sw=4 et tw=99:
 *
 * ***** BEGIN LICENSE BLOCK *****
 * Version: MPL 1.1/GPL 2.0/LGPL 2.1
 *
 * The contents of this file are subject to the Mozilla Public License Version
 * 1.1 (the "License"); you may not use this file except in compliance with
 * the License. You may obtain a copy of the License at
 * http://www.mozilla.org/MPL/
 *
 * Software distributed under the License is distributed on an "AS IS" basis,
 * WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License
 * for the specific language governing rights and limitations under the
 * License.
 *
 * The Original Code is Mozilla Communicator client code, released
 * March 31, 1998.
 *
 * The Initial Developer of the Original Code is
 * Netscape Communications Corporation.
 * Portions created by the Initial Developer are Copyright (C) 1998-2011
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *
 * Alternatively, the contents of this file may be used under the terms of
 * either the GNU General Public License Version 2 or later (the "GPL"), or
 * the GNU Lesser General Public License Version 2.1 or later (the "LGPL"),
 * in which case the provisions of the GPL or the LGPL are applicable instead
 * of those above. If you wish to allow use of your version of this file only
 * under the terms of either the GPL or the LGPL, and not to allow others to
 * use your version of this file under the terms of the MPL, indicate your
 * decision by deleting the provisions above and replace them with the notice
 * and other provisions required by the GPL or the LGPL. If you do not delete
 * the provisions above, a recipient may use your version of this file under
 * the terms of any one of the MPL, the GPL or the LGPL.
 *
 * ***** END LICENSE BLOCK ***** */

#ifndef BytecodeCompiler_h__
#define BytecodeCompiler_h__

#include "frontend/Parser.h"

namespace js {
namespace frontend {

bool
CompileFunctionBody(JSContext *cx, JSFunction *fun,
                    JSPrincipals *principals, JSPrincipals *originPrincipals,
                    Bindings *bindings, const jschar *chars, size_t length,
                    const char *filename, uintN lineno, JSVersion version);

JSScript *
CompileScript(JSContext *cx, JSObject *scopeChain, StackFrame *callerFrame,
              JSPrincipals *principals, JSPrincipals *originPrincipals,
              uint32_t tcflags, const jschar *chars, size_t length,
              const char *filename, uintN lineno, JSVersion version,
              JSString *source = NULL, uintN staticLevel = 0);

} /* namespace frontend */
} /* namespace js */

#endif /* BytecodeCompiler_h__ */
