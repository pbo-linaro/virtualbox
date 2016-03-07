; $Id$
;; @file
; BS3Kit - Bs3TestDoModes helpers
;

;
; Copyright (C) 2007-2016 Oracle Corporation
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

%include "bs3kit-template-header.mac"


;*********************************************************************************************************************************
;*  External Symbols                                                                                                             *
;*********************************************************************************************************************************
extern TMPL_NM(Bs3SwitchToRM)
extern TMPL_NM(Bs3SwitchToPE16)
extern TMPL_NM(Bs3SwitchToPE16_32)
extern TMPL_NM(Bs3SwitchToPE16_V86)
extern TMPL_NM(Bs3SwitchToPE32)
extern TMPL_NM(Bs3SwitchToPE32_16)
extern TMPL_NM(Bs3SwitchToPEV86)
extern TMPL_NM(Bs3SwitchToPP16)
extern TMPL_NM(Bs3SwitchToPP16_32)
extern TMPL_NM(Bs3SwitchToPP16_V86)
extern TMPL_NM(Bs3SwitchToPP32)
extern TMPL_NM(Bs3SwitchToPP32_16)
extern TMPL_NM(Bs3SwitchToPPV86)
extern TMPL_NM(Bs3SwitchToPAE16)
extern TMPL_NM(Bs3SwitchToPAE16_32)
extern TMPL_NM(Bs3SwitchToPAE16_V86)
extern TMPL_NM(Bs3SwitchToPAE32)
extern TMPL_NM(Bs3SwitchToPAE32_16)
extern TMPL_NM(Bs3SwitchToPAEV86)
extern TMPL_NM(Bs3SwitchToLM16)
extern TMPL_NM(Bs3SwitchToLM32)
extern TMPL_NM(Bs3SwitchToLM64)
extern RT_CONCAT3(_Bs3SwitchTo,TMPL_MODE_UNAME,_rm)
extern RT_CONCAT3(_Bs3SwitchTo,TMPL_MODE_UNAME,_pe16)
extern RT_CONCAT3(_Bs3SwitchTo,TMPL_MODE_UNAME,_pe16_32)
extern RT_CONCAT3(_Bs3SwitchTo,TMPL_MODE_UNAME,_pe16_v86)
extern RT_CONCAT3(_Bs3SwitchTo,TMPL_MODE_UNAME,_pe32)
extern RT_CONCAT3(_Bs3SwitchTo,TMPL_MODE_UNAME,_pe32_16)
extern RT_CONCAT3(_Bs3SwitchTo,TMPL_MODE_UNAME,_pev86)
extern RT_CONCAT3(_Bs3SwitchTo,TMPL_MODE_UNAME,_pp16)
extern RT_CONCAT3(_Bs3SwitchTo,TMPL_MODE_UNAME,_pp16_32)
extern RT_CONCAT3(_Bs3SwitchTo,TMPL_MODE_UNAME,_pp16_v86)
extern RT_CONCAT3(_Bs3SwitchTo,TMPL_MODE_UNAME,_pp32)
extern RT_CONCAT3(_Bs3SwitchTo,TMPL_MODE_UNAME,_pp32_16)
extern RT_CONCAT3(_Bs3SwitchTo,TMPL_MODE_UNAME,_ppv86)
extern RT_CONCAT3(_Bs3SwitchTo,TMPL_MODE_UNAME,_pae16)
extern RT_CONCAT3(_Bs3SwitchTo,TMPL_MODE_UNAME,_pae16_32)
extern RT_CONCAT3(_Bs3SwitchTo,TMPL_MODE_UNAME,_pae16_v86)
extern RT_CONCAT3(_Bs3SwitchTo,TMPL_MODE_UNAME,_pae32)
extern RT_CONCAT3(_Bs3SwitchTo,TMPL_MODE_UNAME,_pae32_16)
extern RT_CONCAT3(_Bs3SwitchTo,TMPL_MODE_UNAME,_paev86)
extern RT_CONCAT3(_Bs3SwitchTo,TMPL_MODE_UNAME,_lm16)
extern RT_CONCAT3(_Bs3SwitchTo,TMPL_MODE_UNAME,_lm32)
extern RT_CONCAT3(Bs3SwitchTo,TMPL_MODE_UNAME,_lm64)


;;
; Shared prologue code.
; @param    xAX     Where to jump to for the main event.
;
TMPL_NM(bs3TestCallDoerPrologue):
        BS3_CALL_CONV_PROLOG 1
        push    xBP
        mov     xBP, xSP
        xPUSHF

        ; Save non-volatile registers so the DO function doesn't have to.
        push    xBX
        push    xCX
        push    xDX
        push    xSI
        push    xDI
%if TMPL_BITS != 64
        push    ds
        push    es
        push    ss
 %if TMPL_BITS != 16
        push    fs
        push    gs
 %endif
%endif
%if TMPL_BITS == 64
        push    r8
        push    r9
        push    r10
        push    r11
        push    r12
        push    r13
        push    r14
        push    r15
%endif

        ; Jump to the main code.
        jmp     xAX

;;
; Shared epilogue code.
; @param    xAX     Return code.
;
TMPL_NM(bs3TestCallDoerEpilogue):
        ; Restore registers.
%if TMPL_BITS == 16
        sub     bp, (1+5+3)*2
        mov     sp, bp
%elif TMPL_BITS == 32
        lea     xSP, [xBP - (1+5+5)*4]
%else
        lea     xSP, [xBP - (1+5+8)*8]
        pop     r15
        pop     r14
        pop     r13
        pop     r12
        pop     r11
        pop     r10
        pop     r9
        pop     r8
%endif
%if TMPL_BITS != 64
 %if TMPL_BITS != 16
        pop     gs
        pop     fs
 %endif
        pop     ss
        pop     es
        pop     ds
%endif
        pop     xDI
        pop     xSI
        pop     xDX
        pop     xCX
        pop     xBX
        xPOPF
        pop     xBP
        ret


;;
; @cproto   BS3_DECL(uint8_t) Bs3TestCallDoerInRM(uint16_t offBs3Text16);
; @uses     rax
BS3_PROC_BEGIN_MODE Bs3TestCallDoerInRM
        BS3_LEA_MOV_WRT_RIP(xAX, .doit)
        jmp     TMPL_NM(bs3TestCallDoerPrologue)
BS3_BEGIN_TEXT16
BS3_SET_BITS TMPL_BITS
.doit:
        mov     ax, [xBP + xCB*2]       ; Load function pointer.

        ; Mode switch, make the call, switch back.
        call    TMPL_NM(Bs3SwitchToRM)
        BS3_SET_BITS 16
        call    ax
        call    RT_CONCAT3(_Bs3SwitchTo,TMPL_MODE_UNAME,_rm)
        BS3_SET_BITS TMPL_BITS
        jmp     TMPL_NM(bs3TestCallDoerEpilogue)
TMPL_BEGIN_TEXT
BS3_PROC_END_MODE   Bs3TestCallDoerInRM


;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Unpage protection mode.
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

;;
; @cproto   BS3_DECL(uint8_t) Bs3TestCallDoerInPE16(uint16_t offBs3Text16);
; @uses     rax
BS3_PROC_BEGIN_MODE Bs3TestCallDoerInPE16
        BS3_LEA_MOV_WRT_RIP(xAX, .doit)
        jmp     TMPL_NM(bs3TestCallDoerPrologue)
BS3_BEGIN_TEXT16
BS3_SET_BITS TMPL_BITS
.doit:
        mov     ax, [xBP + xCB*2]       ; Load function pointer.

        ; Mode switch, make the call, switch back.
        call    TMPL_NM(Bs3SwitchToPE16)
        BS3_SET_BITS 16
        call    ax
        call    RT_CONCAT3(_Bs3SwitchTo,TMPL_MODE_UNAME,_pe16)
        BS3_SET_BITS TMPL_BITS
        jmp     TMPL_NM(bs3TestCallDoerEpilogue)
TMPL_BEGIN_TEXT
BS3_PROC_END_MODE   Bs3TestCallDoerInPE16

;;
; @cproto   BS3_DECL(uint8_t) Bs3TestCallDoerInPE16_32(uint16_t offBs3Text16);
; @uses     rax
BS3_PROC_BEGIN_MODE Bs3TestCallDoerInPE16_32
        BS3_LEA_MOV_WRT_RIP(xAX, .doit)
        jmp     TMPL_NM(bs3TestCallDoerPrologue)
.doit:
        mov     eax, [xBP + xCB*2]      ; Load function pointer.

        ; Mode switch, make the call, switch back.
        call    TMPL_NM(Bs3SwitchToPE16_32)
        BS3_SET_BITS 32
        call    eax
        call    RT_CONCAT3(_Bs3SwitchTo,TMPL_MODE_UNAME,_pe16_32)
        BS3_SET_BITS TMPL_BITS
        jmp     TMPL_NM(bs3TestCallDoerEpilogue)
BS3_PROC_END_MODE   Bs3TestCallDoerInPE16_32

;;
; @cproto   BS3_DECL(uint8_t) Bs3TestCallDoerInPE16_V86(uint16_t offBs3Text16);
; @uses     rax
BS3_PROC_BEGIN_MODE Bs3TestCallDoerInPE16_V86
        BS3_LEA_MOV_WRT_RIP(xAX, .doit)
        jmp     TMPL_NM(bs3TestCallDoerPrologue)
.doit:
        mov     eax, [xBP + xCB*2]      ; Load function pointer.

        ; Mode switch, make the call, switch back.
        call    TMPL_NM(Bs3SwitchToPE16_V86)
        BS3_SET_BITS 32
        call    eax
        call    RT_CONCAT3(_Bs3SwitchTo,TMPL_MODE_UNAME,_pe16_v86)
        BS3_SET_BITS TMPL_BITS
        jmp     TMPL_NM(bs3TestCallDoerEpilogue)
BS3_PROC_END_MODE   Bs3TestCallDoerInPE16_V86

;;
; @cproto   BS3_DECL(uint8_t) Bs3TestCallDoerInPE32(uint16_t offBs3Text16);
; @uses     rax
BS3_PROC_BEGIN_MODE Bs3TestCallDoerInPE32
        BS3_LEA_MOV_WRT_RIP(xAX, .doit)
        jmp     TMPL_NM(bs3TestCallDoerPrologue)
.doit:
        mov     eax, [xBP + xCB*2]      ; Load function pointer.

        ; Mode switch, make the call, switch back.
        call    TMPL_NM(Bs3SwitchToPE32)
        BS3_SET_BITS 32
        call    eax
        call    RT_CONCAT3(_Bs3SwitchTo,TMPL_MODE_UNAME,_pe32)
        BS3_SET_BITS TMPL_BITS
        jmp     TMPL_NM(bs3TestCallDoerEpilogue)
BS3_PROC_END_MODE   Bs3TestCallDoerInPE32

;;
; @cproto   BS3_DECL(uint8_t) Bs3TestCallDoerInPE32_16(uint16_t offBs3Text16);
; @uses     rax
BS3_PROC_BEGIN_MODE Bs3TestCallDoerInPE32_16
        BS3_LEA_MOV_WRT_RIP(xAX, .doit)
        jmp     TMPL_NM(bs3TestCallDoerPrologue)
BS3_BEGIN_TEXT16
BS3_SET_BITS TMPL_BITS
.doit:
        mov     ax, [xBP + xCB*2]       ; Load function pointer.

        ; Mode switch, make the call, switch back.
        call    TMPL_NM(Bs3SwitchToPE32_16)
        BS3_SET_BITS 16
        call    ax
        call    RT_CONCAT3(_Bs3SwitchTo,TMPL_MODE_UNAME,_pe32_16)
        BS3_SET_BITS TMPL_BITS
        jmp     TMPL_NM(bs3TestCallDoerEpilogue)
TMPL_BEGIN_TEXT
BS3_PROC_END_MODE   Bs3TestCallDoerInPE32_16

;;
; @cproto   BS3_DECL(uint8_t) Bs3TestCallDoerInPEV86(uint16_t offBs3Text16);
; @uses     rax
BS3_PROC_BEGIN_MODE Bs3TestCallDoerInPEV86
        BS3_LEA_MOV_WRT_RIP(xAX, .doit)
        jmp     TMPL_NM(bs3TestCallDoerPrologue)
BS3_BEGIN_TEXT16
BS3_SET_BITS TMPL_BITS
.doit:
        mov     ax, [xBP + xCB*2]       ; Load function pointer.

        ; Mode switch, make the call, switch back.
        call    TMPL_NM(Bs3SwitchToPEV86)
        BS3_SET_BITS 16
        call    ax
        call    RT_CONCAT3(_Bs3SwitchTo,TMPL_MODE_UNAME,_pev86)
        BS3_SET_BITS TMPL_BITS
        jmp     TMPL_NM(bs3TestCallDoerEpilogue)
TMPL_BEGIN_TEXT
BS3_PROC_END_MODE   Bs3TestCallDoerInPEV86



;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Page protection mode.
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

;;
; @cproto   BS3_DECL(uint8_t) Bs3TestCallDoerInPP16(uint16_t offBs3Text16);
; @uses     rax
BS3_PROC_BEGIN_MODE Bs3TestCallDoerInPP16
        BS3_LEA_MOV_WRT_RIP(xAX, .doit)
        jmp     TMPL_NM(bs3TestCallDoerPrologue)
BS3_BEGIN_TEXT16
BS3_SET_BITS TMPL_BITS
.doit:
        mov     ax, [xBP + xCB*2]       ; Load function pointer.

        ; Mode switch, make the call, switch back.
        call    TMPL_NM(Bs3SwitchToPP16)
        BS3_SET_BITS 16
        call    ax
        call    RT_CONCAT3(_Bs3SwitchTo,TMPL_MODE_UNAME,_pp16)
        BS3_SET_BITS TMPL_BITS
        jmp     TMPL_NM(bs3TestCallDoerEpilogue)
TMPL_BEGIN_TEXT
BS3_PROC_END_MODE   Bs3TestCallDoerInPP16

;;
; @cproto   BS3_DECL(uint8_t) Bs3TestCallDoerInPP16_32(uint16_t offBs3Text16);
; @uses     rax
BS3_PROC_BEGIN_MODE Bs3TestCallDoerInPP16_32
        BS3_LEA_MOV_WRT_RIP(xAX, .doit)
        jmp     TMPL_NM(bs3TestCallDoerPrologue)
.doit:
        mov     eax, [xBP + xCB*2]      ; Load function pointer.

        ; Mode switch, make the call, switch back.
        call    TMPL_NM(Bs3SwitchToPP16_32)
        BS3_SET_BITS 32
        call    eax
        call    RT_CONCAT3(_Bs3SwitchTo,TMPL_MODE_UNAME,_pp16_32)
        BS3_SET_BITS TMPL_BITS
        jmp     TMPL_NM(bs3TestCallDoerEpilogue)
BS3_PROC_END_MODE   Bs3TestCallDoerInPP16_32

;;
; @cproto   BS3_DECL(uint8_t) Bs3TestCallDoerInPP16_V86(uint16_t offBs3Text16);
; @uses     rax
BS3_PROC_BEGIN_MODE Bs3TestCallDoerInPP16_V86
        BS3_LEA_MOV_WRT_RIP(xAX, .doit)
        jmp     TMPL_NM(bs3TestCallDoerPrologue)
.doit:
        mov     eax, [xBP + xCB*2]      ; Load function pointer.

        ; Mode switch, make the call, switch back.
        call    TMPL_NM(Bs3SwitchToPP16_V86)
        BS3_SET_BITS 32
        call    eax
        call    RT_CONCAT3(_Bs3SwitchTo,TMPL_MODE_UNAME,_pp16_v86)
        BS3_SET_BITS TMPL_BITS
        jmp     TMPL_NM(bs3TestCallDoerEpilogue)
BS3_PROC_END_MODE   Bs3TestCallDoerInPP16_V86

;;
; @cproto   BS3_DECL(uint8_t) Bs3TestCallDoerInPP32(uint16_t offBs3Text16);
; @uses     rax
BS3_PROC_BEGIN_MODE Bs3TestCallDoerInPP32
        BS3_LEA_MOV_WRT_RIP(xAX, .doit)
        jmp     TMPL_NM(bs3TestCallDoerPrologue)
.doit:
        mov     eax, [xBP + xCB*2]      ; Load function pointer.

        ; Mode switch, make the call, switch back.
        call    TMPL_NM(Bs3SwitchToPP32)
        BS3_SET_BITS 32
        call    eax
        call    RT_CONCAT3(_Bs3SwitchTo,TMPL_MODE_UNAME,_pp32)
        BS3_SET_BITS TMPL_BITS
        jmp     TMPL_NM(bs3TestCallDoerEpilogue)
BS3_PROC_END_MODE   Bs3TestCallDoerInPP32

;;
; @cproto   BS3_DECL(uint8_t) Bs3TestCallDoerInPP32_16(uint16_t offBs3Text16);
; @uses     rax
BS3_PROC_BEGIN_MODE Bs3TestCallDoerInPP32_16
        BS3_LEA_MOV_WRT_RIP(xAX, .doit)
        jmp     TMPL_NM(bs3TestCallDoerPrologue)
BS3_BEGIN_TEXT16
BS3_SET_BITS TMPL_BITS
.doit:
        mov     ax, [xBP + xCB*2]       ; Load function pointer.

        ; Mode switch, make the call, switch back.
        call    TMPL_NM(Bs3SwitchToPP32_16)
        BS3_SET_BITS 16
        call    ax
        call    RT_CONCAT3(_Bs3SwitchTo,TMPL_MODE_UNAME,_pp32_16)
        BS3_SET_BITS TMPL_BITS
        jmp     TMPL_NM(bs3TestCallDoerEpilogue)
TMPL_BEGIN_TEXT
BS3_PROC_END_MODE   Bs3TestCallDoerInPP32_16

;;
; @cproto   BS3_DECL(uint8_t) Bs3TestCallDoerInPPV86(uint16_t offBs3Text16);
; @uses     rax
BS3_PROC_BEGIN_MODE Bs3TestCallDoerInPPV86
        BS3_LEA_MOV_WRT_RIP(xAX, .doit)
        jmp     TMPL_NM(bs3TestCallDoerPrologue)
BS3_BEGIN_TEXT16
BS3_SET_BITS TMPL_BITS
.doit:
        mov     ax, [xBP + xCB*2]       ; Load function pointer.

        ; Mode switch, make the call, switch back.
        call    TMPL_NM(Bs3SwitchToPPV86)
        BS3_SET_BITS 16
        call    ax
        call    RT_CONCAT3(_Bs3SwitchTo,TMPL_MODE_UNAME,_ppv86)
        BS3_SET_BITS TMPL_BITS
        jmp     TMPL_NM(bs3TestCallDoerEpilogue)
TMPL_BEGIN_TEXT
BS3_PROC_END_MODE   Bs3TestCallDoerInPPV86



;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; PAE paged protection mode.
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

;;
; @cproto   BS3_DECL(uint8_t) Bs3TestCallDoerInPAE16(uint16_t offBs3Text16);
; @uses     rax
BS3_PROC_BEGIN_MODE Bs3TestCallDoerInPAE16
        BS3_LEA_MOV_WRT_RIP(xAX, .doit)
        jmp     TMPL_NM(bs3TestCallDoerPrologue)
BS3_BEGIN_TEXT16
BS3_SET_BITS TMPL_BITS
.doit:
        mov     ax, [xBP + xCB*2]       ; Load function pointer.

        ; Mode switch, make the call, switch back.
        call    TMPL_NM(Bs3SwitchToPAE16)
        BS3_SET_BITS 16
        call    ax
        call    RT_CONCAT3(_Bs3SwitchTo,TMPL_MODE_UNAME,_pae16)
        BS3_SET_BITS TMPL_BITS
        jmp     TMPL_NM(bs3TestCallDoerEpilogue)
TMPL_BEGIN_TEXT
BS3_PROC_END_MODE   Bs3TestCallDoerInPAE16

;;
; @cproto   BS3_DECL(uint8_t) Bs3TestCallDoerInPAE16_32(uint16_t offBs3Text16);
; @uses     rax
BS3_PROC_BEGIN_MODE Bs3TestCallDoerInPAE16_32
        BS3_LEA_MOV_WRT_RIP(xAX, .doit)
        jmp     TMPL_NM(bs3TestCallDoerPrologue)
.doit:
        mov     eax, [xBP + xCB*2]      ; Load function pointer.

        ; Mode switch, make the call, switch back.
        call    TMPL_NM(Bs3SwitchToPAE16_32)
        BS3_SET_BITS 32
        call    eax
        call    RT_CONCAT3(_Bs3SwitchTo,TMPL_MODE_UNAME,_pae16_32)
        BS3_SET_BITS TMPL_BITS
        jmp     TMPL_NM(bs3TestCallDoerEpilogue)
BS3_PROC_END_MODE   Bs3TestCallDoerInPAE16_32

;;
; @cproto   BS3_DECL(uint8_t) Bs3TestCallDoerInPAE16_V86(uint16_t offBs3Text16);
; @uses     rax
BS3_PROC_BEGIN_MODE Bs3TestCallDoerInPAE16_V86
        BS3_LEA_MOV_WRT_RIP(xAX, .doit)
        jmp     TMPL_NM(bs3TestCallDoerPrologue)
.doit:
        mov     eax, [xBP + xCB*2]      ; Load function pointer.

        ; Mode switch, make the call, switch back.
        call    TMPL_NM(Bs3SwitchToPAE16_V86)
        BS3_SET_BITS 32
        call    eax
        call    RT_CONCAT3(_Bs3SwitchTo,TMPL_MODE_UNAME,_pae16_v86)
        BS3_SET_BITS TMPL_BITS
        jmp     TMPL_NM(bs3TestCallDoerEpilogue)
BS3_PROC_END_MODE   Bs3TestCallDoerInPAE16_V86

;;
; @cproto   BS3_DECL(uint8_t) Bs3TestCallDoerInPAE32(uint16_t offBs3Text16);
; @uses     rax
BS3_PROC_BEGIN_MODE Bs3TestCallDoerInPAE32
        BS3_LEA_MOV_WRT_RIP(xAX, .doit)
        jmp     TMPL_NM(bs3TestCallDoerPrologue)
.doit:
        mov     eax, [xBP + xCB*2]      ; Load function pointer.

        ; Mode switch, make the call, switch back.
        call    TMPL_NM(Bs3SwitchToPAE32)
        BS3_SET_BITS 32
        call    eax
        call    RT_CONCAT3(_Bs3SwitchTo,TMPL_MODE_UNAME,_pae32)
        BS3_SET_BITS TMPL_BITS
        jmp     TMPL_NM(bs3TestCallDoerEpilogue)
BS3_PROC_END_MODE   Bs3TestCallDoerInPAE32

;;
; @cproto   BS3_DECL(uint8_t) Bs3TestCallDoerInPAE32_16(uint16_t offBs3Text16);
; @uses     rax
BS3_PROC_BEGIN_MODE Bs3TestCallDoerInPAE32_16
        BS3_LEA_MOV_WRT_RIP(xAX, .doit)
        jmp     TMPL_NM(bs3TestCallDoerPrologue)
BS3_BEGIN_TEXT16
BS3_SET_BITS TMPL_BITS
.doit:
        mov     ax, [xBP + xCB*2]       ; Load function pointer.

        ; Mode switch, make the call, switch back.
        call    TMPL_NM(Bs3SwitchToPAE32_16)
        BS3_SET_BITS 16
        call    ax
        call    RT_CONCAT3(_Bs3SwitchTo,TMPL_MODE_UNAME,_pae32_16)
        BS3_SET_BITS TMPL_BITS
        jmp     TMPL_NM(bs3TestCallDoerEpilogue)
TMPL_BEGIN_TEXT
BS3_PROC_END_MODE   Bs3TestCallDoerInPAE32_16

;;
; @cproto   BS3_DECL(uint8_t) Bs3TestCallDoerInPAEV86(uint16_t offBs3Text16);
; @uses     rax
BS3_PROC_BEGIN_MODE Bs3TestCallDoerInPAEV86
        BS3_LEA_MOV_WRT_RIP(xAX, .doit)
        jmp     TMPL_NM(bs3TestCallDoerPrologue)
BS3_BEGIN_TEXT16
BS3_SET_BITS TMPL_BITS
.doit:
        mov     ax, [xBP + xCB*2]       ; Load function pointer.

        ; Mode switch, make the call, switch back.
        call    TMPL_NM(Bs3SwitchToPAEV86)
        BS3_SET_BITS 16
        call    ax
        call    RT_CONCAT3(_Bs3SwitchTo,TMPL_MODE_UNAME,_paev86)
        BS3_SET_BITS TMPL_BITS
        jmp     TMPL_NM(bs3TestCallDoerEpilogue)
TMPL_BEGIN_TEXT
BS3_PROC_END_MODE   Bs3TestCallDoerInPAEV86



;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Long mode
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

;;
; @cproto   BS3_DECL(uint8_t) Bs3TestCallDoerInLM16(uint16_t offBs3Text16);
; @uses     rax
BS3_PROC_BEGIN_MODE Bs3TestCallDoerInLM16
        BS3_LEA_MOV_WRT_RIP(xAX, .doit)
        jmp     TMPL_NM(bs3TestCallDoerPrologue)
BS3_BEGIN_TEXT16
BS3_SET_BITS TMPL_BITS
.doit:
        mov     ax, [xBP + xCB*2]       ; Load function pointer.

        ; Mode switch, make the call, switch back.
        call    TMPL_NM(Bs3SwitchToLM16)
        BS3_SET_BITS 16
        call    ax
        call    RT_CONCAT3(_Bs3SwitchTo,TMPL_MODE_UNAME,_lm16)
        BS3_SET_BITS TMPL_BITS
        jmp     TMPL_NM(bs3TestCallDoerEpilogue)
TMPL_BEGIN_TEXT
BS3_PROC_END_MODE   Bs3TestCallDoerInLM16

;;
; @cproto   BS3_DECL(uint8_t) Bs3TestCallDoerInLM32(uint16_t offBs3Text16);
; @uses     rax
BS3_PROC_BEGIN_MODE Bs3TestCallDoerInLM32
        BS3_LEA_MOV_WRT_RIP(xAX, .doit)
        jmp     TMPL_NM(bs3TestCallDoerPrologue)
.doit:
        mov     eax, [xBP + xCB*2]      ; Load function pointer.

        ; Mode switch, make the call, switch back.
        call    TMPL_NM(Bs3SwitchToLM32)
        BS3_SET_BITS 32
        call    eax
        call    RT_CONCAT3(_Bs3SwitchTo,TMPL_MODE_UNAME,_lm32)
        BS3_SET_BITS TMPL_BITS
        jmp     TMPL_NM(bs3TestCallDoerEpilogue)
BS3_PROC_END_MODE   Bs3TestCallDoerInLM32

;;
; @cproto   BS3_DECL(uint8_t) Bs3TestCallDoerInLM64(uint16_t offBs3Text16);
; @uses     rax
BS3_PROC_BEGIN_MODE Bs3TestCallDoerInLM64
        BS3_LEA_MOV_WRT_RIP(xAX, .doit)
        jmp     TMPL_NM(bs3TestCallDoerPrologue)
.doit:
        mov     eax, [xBP + xCB*2]      ; Load function pointer.

        ; Mode switch, make the call, switch back.
        call    TMPL_NM(Bs3SwitchToLM64)
        BS3_SET_BITS 64
        call    rax
        call    RT_CONCAT3(Bs3SwitchTo,TMPL_MODE_UNAME,_lm64)
        BS3_SET_BITS TMPL_BITS
        jmp     TMPL_NM(bs3TestCallDoerEpilogue)
BS3_PROC_END_MODE   Bs3TestCallDoerInLM64

