; $Id$
;; @file
; IPRT - No-CRT fegetexceptflag - AMD64 & X86.
;

;
; Copyright (C) 2022 Oracle Corporation
;
; This file is part of VirtualBox Open Source Edition (OSE), as
; available from http://www.virtualbox.org. This file is free software;
; you can redistribute it and/or modify it under the terms of the GNU
; General Public License (GPL) as published by the Free Software
; Foundation, in version 2 as it comes in the "COPYING" file of the
; VirtualBox OSE distribution. VirtualBox OSE is distributed in the
; hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
;
; The contents of this file may alternatively be used under the terms
; of the Common Development and Distribution License Version 1.0
; (CDDL) only, as it comes in the "COPYING.CDDL" file of the
; VirtualBox OSE distribution, in which case the provisions of the
; CDDL are applicable instead of those of the GPL.
;
; You may elect to license modified versions of this file under the
; terms and conditions of either the GPL or the CDDL or both.
;


%define RT_ASM_WITH_SEH64
%include "iprt/asmdefs.mac"
%include "iprt/x86.mac"


BEGINCODE

;;
; Gets the pending exceptions.
;
; @returns  eax = 0 on success, non-zero on failure.
; @param    pfXcpts     32-bit: [xBP+8]; msc64: rcx; gcc64: rdi; -- Where to store the flags (pointer to fexcept_t (16-bit)).
; @param    fXcptMask   32-bit: [xBP+c]; msc64: edx; gcc64: esi; -- The exception flags to get (X86_FSW_XCPT_MASK).
;                       Accepts X86_FSW_SF and will return it if given as input.
;
RT_NOCRT_BEGINPROC fegetexceptflag
        push    xBP
        SEH64_PUSH_xBP
        mov     xBP, xSP
        SEH64_SET_FRAME_xBP 0
        sub     xSP, 10h
        SEH64_ALLOCATE_STACK 10h
        SEH64_END_PROLOGUE

        ;
        ; Load the parameter into rcx (pfXcpts) and edx (fXcptMask).
        ;
%ifdef ASM_CALL64_GCC
        mov     rcx, rdi
        mov     edx, esi
%elifdef RT_ARCH_X86
        mov     ecx, [xBP + xCB*2]
        mov     edx, [xBP + xCB*3]
%endif
%if 0
        and     edx, X86_FSW_XCPT_MASK
%else
        or      eax, -1
        test    edx, ~X86_FSW_XCPT_MASK
 %ifndef RT_STRICT
        jnz     .return
 %else
        jz      .input_ok
        int3
        jmp     .return
.input_ok:
 %endif
%endif

        ;
        ; Get the pending exceptions.
        ;

        ; x87.
        fnstsw  ax
        and     ax, dx
        mov     [xCX], ax

%ifdef RT_ARCH_X86
        ; SSE supported (ecx preserved)?
        extern  NAME(rtNoCrtHasSse)
        call    NAME(rtNoCrtHasSse)
        test    al, al
        jz      .return_ok
%endif

        ; Modify the SSE flags.
        stmxcsr [xBP - 10h]
        mov     ax, [xBP - 10h]
        and     ax, dx
        and     ax, X86_MXCSR_XCPT_FLAGS        ; Don't confuse X86_MXCSR_DAZ for X86_FSW_SF.
        or      [xCX], ax

.return_ok:
        xor     eax, eax
.return:
        leave
        ret
ENDPROC   RT_NOCRT(fegetexceptflag)

