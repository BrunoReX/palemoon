/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*-
 * vim: set ts=4 sw=4 et tw=99:
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
 * The Original Code is Mozilla Jaegermonkey.
 *
 * The Initial Developer of the Original Code is the Mozilla Foundation.
 *
 * Portions created by the Initial Developer are Copyright (C) 2010
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *   Andrew Drake <drakedevel@gmail.com>
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

/*
 * Retroactive continuity ("retcon") refers to the retroactive modification
 * or reinterpretation of established facts. 
 */

#if !defined jsjaeger_retcon_h__ && defined JS_METHODJIT
#define jsjaeger_retcon_h__

#include "jscntxt.h"
#include "jsscript.h"
#include "MethodJIT.h"
#include "Compiler.h"

namespace js {
namespace mjit {

/*
 * This class is responsible for sanely destroying a JITed script while frames
 * for it are still on the stack, removing all references in the world to it
 * and patching up those existing frames to go into the interpreter. If you
 * ever change the code associated with a JSScript, or otherwise would cause
 * existing JITed code to be incorrect, you /must/ use this to invalidate the
 * JITed code, fixing up the stack in the process.
 */
class Recompiler {
public:
    Recompiler(JSContext *cx, JSScript *script);

    void recompile(bool resetUses = true);

    static void
    expandInlineFrames(JSCompartment *compartment, StackFrame *fp, mjit::CallSite *inlined,
                       StackFrame *next, VMFrame *f);

    static void patchFrame(JSCompartment *compartment, VMFrame *f, JSScript *script);

private:
    JSContext *cx;
    JSScript *script;

    static void patchCall(JITScript *jit, StackFrame *fp, void **location);
    static void patchNative(JSCompartment *compartment, JITScript *jit, StackFrame *fp,
                            jsbytecode *pc, RejoinState rejoin);

    static StackFrame *
    expandInlineFrameChain(StackFrame *outer, InlineFrame *inner);

    /* Detach jit from any IC callers. */
    static void cleanup(JITScript *jit);
};

} /* namespace mjit */
} /* namespace js */

#endif

