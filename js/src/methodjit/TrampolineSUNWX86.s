/ -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*-
/ ***** BEGIN LICENSE BLOCK *****
/ Version: MPL 1.1/GPL 2.0/LGPL 2.1
/
/ The contents of this file are subject to the Mozilla Public License Version
/ 1.1 (the "License")/ you may not use this file except in compliance with
/ the License. You may obtain a copy of the License at
/ http://www.mozilla.org/MPL/
/
/ Software distributed under the License is distributed on an "AS IS" basis,
/ WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License
/ for the specific language governing rights and limitations under the
/ License.
/
/ The Original Code is mozilla.org code.
/
/ The Initial Developer of the Original Code is Mozilla Japan.
/ Portions created by the Initial Developer are Copyright (C) 2010
/ the Initial Developer. All Rights Reserved.
/
/ Contributor(s):
/   Leon Sha <leon.sha@sun.com>
/
/ Alternatively, the contents of this file may be used under the terms of
/ either the GNU General Public License Version 2 or later (the "GPL"), or
/ the GNU Lesser General Public License Version 2.1 or later (the "LGPL"),
/ in which case the provisions of the GPL or the LGPL are applicable instead
/ of those above. If you wish to allow use of your version of this file only
/ under the terms of either the GPL or the LGPL, and not to allow others to
/ use your version of this file under the terms of the MPL, indicate your
/ decision by deleting the provisions above and replace them with the notice
/ and other provisions required by the GPL or the LGPL. If you do not delete
/ the provisions above, a recipient may use your version of this file under
/ the terms of any one of the MPL, the GPL or the LGPL.
/
/ ***** END LICENSE BLOCK *****

.text

/ JSBool JaegerTrampoline(JSContext *cx, StackFrame *fp, void *code,
/                         Value *stackLimit)
.global JaegerTrampoline
.type   JaegerTrampoline, @function
JaegerTrampoline:
    /* Prologue. */
    pushl %ebp
    movl %esp, %ebp
    /* Save non-volatile registers. */
    pushl %esi
    pushl %edi
    pushl %ebx

    /* Build the JIT frame. Push fields in order, */
    /* then align the stack to form esp == VMFrame. */
    movl  12(%ebp), %ebx                       /* load fp */
    pushl %ebx                                 /* unused1 */
    pushl %ebx                                 /* unused0 */
    pushl $0x0                                 /* stubRejoin */
    pushl %ebx                                 /* entryncode */
    pushl %ebx                                 /* entryfp */
    pushl 20(%ebp)                             /* stackLimit */
    pushl 8(%ebp)                              /* cx */
    pushl %ebx                                 /* fp */
    subl $0x1C, %esp

    /* Jump into the JIT'd code. */
    /* No fastcall for sunstudio. */
    pushl %esp
    call SetVMFrameRegs
    call PushActiveVMFrame
    popl  %edx

    movl 28(%esp), %ebp                       /* load fp for JIT code */
    jmp  *88(%esp)
.size   JaegerTrampoline, . - JaegerTrampoline

/ void JaegerTrampolineReturn()
.global JaegerTrampolineReturn
.type   JaegerTrampolineReturn, @function
JaegerTrampolineReturn:
    movl  %esi, 0x18(%ebp)
    movl  %edi, 0x1C(%ebp)
    movl  %esp, %ebp
    addl  $0x48, %ebp
    pushl %esp
    call PopActiveVMFrame

    addl $0x40, %esp
    popl %ebx
    popl %edi
    popl %esi
    popl %ebp
    movl $1, %eax
    ret
.size   JaegerTrampolineReturn, . - JaegerTrampolineReturn


/ void *JaegerThrowpoline(js::VMFrame *vmFrame)
.global JaegerThrowpoline
.type   JaegerThrowpoline, @function
JaegerThrowpoline:
    /* For Sun Studio there is no fast call. */
    /* We add the stack by 16 before. */
    addl $0x10, %esp
    /* Align the stack to 16 bytes. */
    pushl %esp 
    pushl (%esp)
    pushl (%esp)
    pushl (%esp)
    call js_InternalThrow
    /* Bump the stack by 0x2c, as in the basic trampoline, but */
    /* also one more word to clean up the stack for jsl_InternalThrow,*/
    /* and another to balance the alignment above. */
    addl $0x10, %esp
    testl %eax, %eax
    je   throwpoline_exit
    jmp  *%eax
throwpoline_exit:
    pushl %esp
    call PopActiveVMFrame
    addl $0x40, %esp
    popl %ebx
    popl %edi
    popl %esi
    popl %ebp
    xorl %eax, %eax
    ret
.size   JaegerThrowpoline, . - JaegerThrowpoline

/ void JaegerInterpoline()
.global JaegerInterpoline
.type   JaegerInterpoline, @function
JaegerInterpoline:
    /* For Sun Studio there is no fast call. */
    /* We add the stack by 16 before. */
    addl $0x10, %esp
    /* Align the stack to 16 bytes. */
    pushl %esp
    pushl %eax
    pushl %edi
    pushl %esi
    call js_InternalInterpret
    addl $0x10, %esp
    movl 0x1C(%esp), %ebp    /* Load frame */
    movl 0x18(%ebp), %esi    /* Load rval payload */
    movl 0x1C(%ebp), %edi    /* Load rval type */
    movl 0xC(%esp), %ecx     /* Load scratch -> argc, for any scripted call */
    testl %eax, %eax
    je   interpoline_exit
    jmp  *%eax
interpoline_exit:
    pushl %esp
    call PopActiveVMFrame
    addl $0x40, %esp
    popl %ebx
    popl %edi
    popl %esi
    popl %ebp
    xorl %eax, %eax
    ret
.size   JaegerInterpoline, . - JaegerInterpoline

/ void JaegerInterpolineScripted()
.global JaegerInterpolineScripted
.type   JaegerInterpolineScripted, @function
JaegerInterpolineScripted:
    movl 0x10(%ebp), %ebp
    movl %ebp, 0x1C(%esp)
    subl $0x10, %esp
    jmp JaegerInterpoline
.size   JaegerInterpolineScripted, . - JaegerInterpolineScripted

/ void JaegerInterpolinePatched()
.global JaegerInterpolinePatched
.type   JaegerInterpolinePatched, @function
JaegerInterpolinePatched:
    subl $0x10, %esp
    jmp JaegerInterpoline
.size   JaegerInterpolinePatched, . - JaegerInterpolinePatched
