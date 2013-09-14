/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 4 -*-
 * vim: set ts=8 sts=4 et sw=4 tw=99:
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef jit_x86_BaselineRegisters_x86_h
#define jit_x86_BaselineRegisters_x86_h

#ifdef JS_ION

#include "jit/IonMacroAssembler.h"

namespace js {
namespace jit {

static const Register BaselineFrameReg = ebp;
static const Register BaselineStackReg = esp;

// ValueOperands R0, R1, and R2
static const ValueOperand R0(ecx, edx);
static const ValueOperand R1(eax, ebx);
static const ValueOperand R2(esi, edi);

// BaselineTailCallReg and BaselineStubReg reuse
// registers from R2.
static const Register BaselineTailCallReg = esi;
static const Register BaselineStubReg     = edi;

static const Register ExtractTemp0        = InvalidReg;
static const Register ExtractTemp1        = InvalidReg;

// FloatReg0 must be equal to ReturnFloatReg.
static const FloatRegister FloatReg0      = xmm0;
static const FloatRegister FloatReg1      = xmm1;

} // namespace jit
} // namespace js

#endif // JS_ION

#endif /* jit_x86_BaselineRegisters_x86_h */
