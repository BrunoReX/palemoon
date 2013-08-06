! -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*-
! ***** BEGIN LICENSE BLOCK *****
! Version: MPL 1.1!GPL 2.0!LGPL 2.1
!
! The contents of this file are subject to the Mozilla Public License Version
! 1.1 (the "License")! you may not use this file except in compliance with
! the License. You may obtain a copy of the License at
! http:!!www.mozilla.org!MPL!
!
! Software distributed under the License is distributed on an "AS IS" basis,
! WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License
! for the specific language governing rights and limitations under the
! License.
!
! The Original Code is Mozilla SpiderMonkey JavaScript 1.9 code, released
! May 28, 2008.
! 
! The Initial Developer of the Original Code is
!    Leon Sha <leon.sha@oracle.com>
!
! Portions created by the Initial Developer are Copyright (C) 2010-2011
! the Initial Developer. All Rights Reserved.
!
! Contributor(s):
!
! Alternatively, the contents of this file may be used under the terms of
! either the GNU General Public License Version 2 or later (the "GPL"), or
! the GNU Lesser General Public License Version 2.1 or later (the "LGPL"),
! in which case the provisions of the GPL or the LGPL are applicable instead
! of those above. If you wish to allow use of your version of this file only
! under the terms of either the GPL or the LGPL, and not to allow others to
! use your version of this file under the terms of the MPL, indicate your
! decision by deleting the provisions above and replace them with the notice
! and other provisions required by the GPL or the LGPL. If you do not delete
! the provisions above, a recipient may use your version of this file under
! the terms of any one of the MPL, the GPL or the LGPL.
!
! ***** END LICENSE BLOCK *****

.text

! JSBool JaegerTrampoline(JSContext *cx, JSStackFrame *fp, void *code,
!                        , uintptr_t inlineCallCount)
.global JaegerTrampoline
.type   JaegerTrampoline, #function
JaegerTrampoline:
    save    %sp,-160,%sp
    st      %i3, [%fp - 20]         ! stackLimit
    st      %i0, [%fp - 24]        ! cx
    st      %i1, [%fp - 16]         ! entryFp
    st      %i1, [%fp - 28]        ! regs->fp
    mov     %i1, %l0               ! fp
    call    SetVMFrameRegs
    mov     %sp, %o0
    call    PushActiveVMFrame
    mov     %sp, %o0
    jmp     %i2
    st      %i7, [%fp - 12]         ! return address
.size   JaegerTrampoline, . - JaegerTrampoline

! void JaegerTrampolineReturn()
.global JaegerTrampolineReturn
.type   JaegerTrampolineReturn, #function
JaegerTrampolineReturn:
    st      %i0, [%l0 + 0x18]                        /* fp->rval type */
    st      %i1, [%l0 + 0x1c]                        /* fp->rval data */
    call    PopActiveVMFrame
    mov     %sp, %o0
    ld      [%fp - 12], %i7         ! return address
    mov     1, %i0
    ret
    restore		
.size   JaegerTrampolineReturn, . - JaegerTrampolineReturn

! void *JaegerThrowpoline(js::VMFrame *vmFrame)
.global JaegerThrowpoline
.type   JaegerThrowpoline, #function
JaegerThrowpoline:
    call    js_InternalThrow
    mov     %sp,%o0
    tst     %o0
    be      throwpoline_exit
    nop
    jmp     %o0
    nop
throwpoline_exit:
    ta      3
    mov     %sp, %o2
    mov     %fp, %o3
    ldd     [%o2 + (0*8)], %l0
    ldd     [%o2 + (1*8)], %l2
    ldd     [%o2 + (2*8)], %l4
    ldd     [%o2 + (3*8)], %l6
    ldd     [%o2 + (4*8)], %i0
    ldd     [%o2 + (5*8)], %i2
    ldd     [%o2 + (6*8)], %i4
    ldd     [%o2 + (7*8)], %i6
    ld      [%o3 - 12], %i7         ! return address
    mov     %o2, %sp
    call    PopActiveVMFrame
    mov     %sp, %o0
    clr     %i0
    ret
    restore
.size   JaegerThrowpoline, . - JaegerThrowpoline

.global InjectJaegerReturn
.type   InjectJaegerReturn, #function
InjectJaegerReturn:
    ld      [%l0 + 0x18], %i0                        /* fp->rval type */
    ld      [%l0 + 0x1c], %i1                        /* fp->rval data */
    ld      [%l0 + 0x14], %i7                        /* fp->ncode */
    sub     %i7, 8, %i7
    ld      [%fp - 28], %l0
    ret
    nop
.size   InjectJaegerReturn, . - InjectJaegerReturn
