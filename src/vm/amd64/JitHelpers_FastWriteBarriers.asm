; Licensed to the .NET Foundation under one or more agreements.
; The .NET Foundation licenses this file to you under the MIT license.
; See the LICENSE file in the project root for more information.

; ==++==
;

;
; ==--==
; ***********************************************************************
; File: JitHelpers_FastWriteBarriers.asm, see jithelp.asm for history
;
; Notes: these are the fast write barriers which are copied in to the
;        JIT_WriteBarrier buffer (found in JitHelpers_Fast.asm).
;        This code should never be executed at runtime and should end
;        up effectively being treated as data.
; ***********************************************************************

include AsmMacros.inc
include asmconstants.inc

extern JIT_InternalThrow:proc
MIN_SIZE equ 28h
E_ACCESSDENIED equ 80070005h

; Two super fast helpers that together do the work of JIT_WriteBarrier.  These
; use inlined ephemeral region bounds and an inlined pointer to the card table.
;
; Until the GC does some major reshuffling, the ephemeral region will always be
; at the top of the heap, so given that we know the reference is inside the
; heap, we don't have to check against the upper bound of the ephemeral region
; (PreGrow version).  Once the GC moves the ephemeral region, this will no longer
; be valid, so we use the PostGrow version to check both the upper and lower
; bounds. The inlined bounds and card table pointers have to be patched
; whenever they change.
;
; At anyone time, the memory pointed to by JIT_WriteBarrier will contain one
; of these functions.  See StompWriteBarrierResize and StompWriteBarrierEphemeral
; in VM\AMD64\JITInterfaceAMD64.cpp and InitJITHelpers1 in VM\JITInterfaceGen.cpp
; for more info.
;
; READ THIS!!!!!!
; it is imperative that the addresses of of the values that we overwrite
; (card table, ephemeral region ranges, etc) are naturally aligned since
; there are codepaths that will overwrite these values while the EE is running.
;
LEAF_ENTRY JIT_WriteBarrier_PreGrow32, _TEXT
        align 4
        ; Do the move into the GC .  It is correct to take an AV here, the EH code
        ; figures out that this came from a WriteBarrier and correctly maps it back
        ; to the managed method which called the WriteBarrier (see setup in
        ; InitializeExceptionHandling, vm\exceptionhandling.cpp).
		 mov     rax, rdx
		xor     rax,rcx
		mov   r10,1
		shl   r10,42
		test   rax, r10
		jnz    MixedArenaGC
;		bt      rax,42
;		jc	    MixedArenaGC
		test   rcx, r10
		jz   NotMixedArenaGC
;	    bt      rcx,42
;		jnc      NotMixedArenaGC
		shr     rax,32
		and      eax,3ffh
		jne      MixedArenaGC
		; valid arena write barrier
		mov     [rcx], rdx
		ret
NotMixedArenaGC:
        mov     [rcx], rdx

        ;NOP_3_BYTE ; padding for alignment of constant

		
PATCH_LABEL JIT_WriteBarrier_PreGrow32_PatchLabel_Lower
        cmp     rdx, 0F0F0F0F0h
        jb      Exit

        shr     rcx, 0Bh
PATCH_LABEL JIT_WriteBarrier_PreGrow32_PatchLabel_CardTable_Check
        cmp     byte ptr [rcx + 0F0F0F0F0h], 0FFh
        jne     UpdateCardTable
        REPRET

        nop ; padding for alignment of constant

PATCH_LABEL JIT_WriteBarrier_PreGrow32_PatchLabel_CardTable_Update
    UpdateCardTable:
        mov     byte ptr [rcx + 0F0F0F0F0h], 0FFh
        ret
	MixedArenaGC:
	mov     [rcx], rdx

		mov     rcx, E_ACCESSDENIED ; access denied 
        add     rsp, MIN_SIZE
        ; void JIT_InternalThrow(unsigned exceptNum)
        jmp     JIT_InternalThrow

    align 16
    Exit:
        REPRET
LEAF_END_MARKED JIT_WriteBarrier_PreGrow32, _TEXT


LEAF_ENTRY JIT_WriteBarrier_PreGrow64, _TEXT
        align 8
        ; Do the move into the GC .  It is correct to take an AV here, the EH code
        ; figures out that this came from a WriteBarrier and correctly maps it back
        ; to the managed method which called the WriteBarrier (see setup in
        ; InitializeExceptionHandling, vm\exceptionhandling.cpp).
		mov     rax, rdx
		xor     rax,rcx
		mov     r10, 1
		shl     r10,42
		test    rax, r10
		jnz     MixedArenaGC2
;		bt      rax,42
;		jc	    MixedArenaGC2
		test    rcx, r10
		jz      NotMixedArenaGC2
;	    bt      rcx,42
;		jnc      NotMixedArenaGC2
		shr     rax,32
		and      eax,3ffh
		jne      MixedArenaGC2
		; valid arena write barrier
		mov     [rcx], rdx
		ret
 
NotMixedArenaGC2:
        mov     [rcx], rdx
         NOP; padding for alignment of constant

        ; Can't compare a 64 bit immediate, so we have to move it into a
        ; register.  Value of this immediate will be patched at runtime.
PATCH_LABEL JIT_WriteBarrier_PreGrow64_Patch_Label_Lower
        mov     rax, 0F0F0F0F0F0F0F0F0h

        ; Check the lower ephemeral region bound.
        cmp     rdx, rax
        jb      Exit

        nop ; padding for alignment of constant

PATCH_LABEL JIT_WriteBarrier_PreGrow64_Patch_Label_CardTable
        mov     rax, 0F0F0F0F0F0F0F0F0h

        ; Touch the card table entry, if not already dirty.
        shr     rcx, 0Bh
        cmp     byte ptr [rcx + rax], 0FFh
        jne     UpdateCardTable
        REPRET

    UpdateCardTable:
        mov     byte ptr [rcx + rax], 0FFh
        ret
MixedArenaGC2:
mov     [rcx], rdx

		mov     rcx, E_ACCESSDENIED ; access denied 
        add     rsp, MIN_SIZE
        ; void JIT_InternalThrow(unsigned exceptNum)
        jmp     JIT_InternalThrow

    align 16
    Exit:
        REPRET
LEAF_END_MARKED JIT_WriteBarrier_PreGrow64, _TEXT


; See comments for JIT_WriteBarrier_PreGrow (above).
LEAF_ENTRY JIT_WriteBarrier_PostGrow64, _TEXT
        align 8
		 mov     rax, rdx
		xor     rax,rcx
		mov   r10,1
		shl   r10,42
		test   rax, r10
		jnz    MixedArenaGC3
;		bt      rax,42
;		jc	    MixedArenaGC3
		test   rcx, r10
		jz   NotMixedArenaGC3
;	    bt      rcx,42
;		jnc      NotMixedArenaGC3
		shr     rax,32
		and      eax,3ffh
		jne      MixedArenaGC3
		; valid arena write barrier
		mov     [rcx], rdx
		ret
NotMixedArenaGC3:
        ; Do the move into the GC .  It is correct to take an AV here, the EH code
        ; figures out that this came from a WriteBarrier and correctly maps it back
        ; to the managed method which called the WriteBarrier (see setup in
        ; InitializeExceptionHandling, vm\exceptionhandling.cpp).
        mov     [rcx], rdx

		NOP ; padding for alignment of constant

        ; Can't compare a 64 bit immediate, so we have to move them into a
        ; register.  Values of these immediates will be patched at runtime.
        ; By using two registers we can pipeline better.  Should we decide to use
        ; a special non-volatile calling convention, this should be changed to
        ; just one.
PATCH_LABEL JIT_WriteBarrier_PostGrow64_Patch_Label_Lower
        mov     rax, 0F0F0F0F0F0F0F0F0h

        ; Check the lower and upper ephemeral region bounds
        cmp     rdx, rax
        jb      Exit

        nop ; padding for alignment of constant

PATCH_LABEL JIT_WriteBarrier_PostGrow64_Patch_Label_Upper
        mov     r8, 0F0F0F0F0F0F0F0F0h

        cmp     rdx, r8
        jae     Exit

        nop ; padding for alignment of constant

PATCH_LABEL JIT_WriteBarrier_PostGrow64_Patch_Label_CardTable
        mov     rax, 0F0F0F0F0F0F0F0F0h

        ; Touch the card table entry, if not already dirty.
        shr     rcx, 0Bh
        cmp     byte ptr [rcx + rax], 0FFh
        jne     UpdateCardTable
        REPRET

    UpdateCardTable:
        mov     byte ptr [rcx + rax], 0FFh
        ret

MixedArenaGC3:
mov     [rcx], rdx

		mov     rcx, E_ACCESSDENIED ; access denied 
        add     rsp, MIN_SIZE
        ; void JIT_InternalThrow(unsigned exceptNum)
        jmp     JIT_InternalThrow

    align 16
    Exit:
        REPRET
LEAF_END_MARKED JIT_WriteBarrier_PostGrow64, _TEXT

LEAF_ENTRY JIT_WriteBarrier_PostGrow32, _TEXT
        align 4
		 mov     rax, rdx
		xor     rax,rcx
				mov   r10,1
		shl   r10,42
		test   rax, r10
		jnz    MixedArenaGC4
;		bt      rax,42
;		jc	    MixedArenaGC4
		test   rcx, r10
		jz   NotMixedArenaGC4
;	    bt      rcx,42
;		jnc      NotMixedArenaGC4
		shr     rax,32
		and      eax,3ffh
		jne      MixedArenaGC4
		; valid arena write barrier
		mov     [rcx], rdx
		ret
NotMixedArenaGC4:
        ; Do the move into the GC .  It is correct to take an AV here, the EH code
        ; figures out that this came from a WriteBarrier and correctly maps it back
        ; to the managed method which called the WriteBarrier (see setup in
        ; InitializeExceptionHandling, vm\exceptionhandling.cpp).
        mov     [rcx], rdx

        ;NOP_3_BYTE ; padding for alignment of constant

        ; Check the lower and upper ephemeral region bounds

PATCH_LABEL JIT_WriteBarrier_PostGrow32_PatchLabel_Lower
        cmp     rdx, 0F0F0F0F0h
        jb      Exit

        NOP_3_BYTE ; padding for alignment of constant

PATCH_LABEL JIT_WriteBarrier_PostGrow32_PatchLabel_Upper
        cmp     rdx, 0F0F0F0F0h
        jae     Exit

        ; Touch the card table entry, if not already dirty.
        shr     rcx, 0Bh

PATCH_LABEL JIT_WriteBarrier_PostGrow32_PatchLabel_CheckCardTable
        cmp     byte ptr [rcx + 0F0F0F0F0h], 0FFh
        jne     UpdateCardTable
        REPRET

        nop ; padding for alignment of constant

PATCH_LABEL JIT_WriteBarrier_PostGrow32_PatchLabel_UpdateCardTable
    UpdateCardTable:
        mov     byte ptr [rcx + 0F0F0F0F0h], 0FFh
        ret
MixedArenaGC4:
mov     [rcx], rdx

		mov     rcx, E_ACCESSDENIED ; access denied 
        add     rsp, MIN_SIZE
        ; void JIT_InternalThrow(unsigned exceptNum)
        jmp     JIT_InternalThrow

    align 16
    Exit:
        REPRET
LEAF_END_MARKED JIT_WriteBarrier_PostGrow32, _TEXT


LEAF_ENTRY JIT_WriteBarrier_SVR32, _TEXT
        align 4
        ;
        ; SVR GC has multiple heaps, so it cannot provide one single 
        ; ephemeral region to bounds check against, so we just skip the
        ; bounds checking all together and do our card table update 
        ; unconditionally.
        ;

		 mov     rax, rdx
		xor     rax,rcx
		mov   r10,1
		shl   r10,42
		test   rax, r10
		jnz    MixedArenaGC5
;		bt      rax,42
;		jc	    MixedArenaGC5
		test   rcx, r10
		jz   NotMixedArenaGC5
;	    bt      rcx,42
;		jnc      NotMixedArenaGC5
		shr     rax,32
		and      eax,3ffh
		jne      MixedArenaGC5
		; valid arena write barrier
		mov     [rcx], rdx
		ret

NotMixedArenaGC5:
        ; Do the move into the GC .  It is correct to take an AV here, the EH code
        ; figures out that this came from a WriteBarrier and correctly maps it back
        ; to the managed method which called the WriteBarrier (see setup in
        ; InitializeExceptionHandling, vm\exceptionhandling.cpp).
        mov     [rcx], rdx

        shr     rcx, 0Bh

        NOP ; padding for alignment of constant

PATCH_LABEL JIT_WriteBarrier_SVR32_PatchLabel_CheckCardTable
        cmp     byte ptr [rcx + 0F0F0F0F0h], 0FFh
        jne     UpdateCardTable
        REPRET

        nop ; padding for alignment of constant

PATCH_LABEL JIT_WriteBarrier_SVR32_PatchLabel_UpdateCardTable
    UpdateCardTable:
        mov     byte ptr [rcx + 0F0F0F0F0h], 0FFh
        ret

MixedArenaGC5:
mov     [rcx], rdx

		mov     rcx, E_ACCESSDENIED ; access denied 
        add     rsp, MIN_SIZE
        ; void JIT_InternalThrow(unsigned exceptNum)
        jmp     JIT_InternalThrow

LEAF_END_MARKED JIT_WriteBarrier_SVR32, _TEXT

LEAF_ENTRY JIT_WriteBarrier_SVR64, _TEXT
        align 8
        ;
        ; SVR GC has multiple heaps, so it cannot provide one single 
        ; ephemeral region to bounds check against, so we just skip the
        ; bounds checking all together and do our card table update 
        ; unconditionally.
        ;
		 mov     rax, rdx
		xor     rax,rcx
		mov   r10,1
		shl   r10,42
		test   rax, r10
		jnz    MixedArenaGC6
;		bt      rax,42
;		jc	    MixedArenaGC6
		test   rcx, r10
		jz   NotMixedArenaGC6
;	    bt      rcx,42
;		jnc      NotMixedArenaGC6
		shr     rax,32
		and      eax,3ffh
		jne      MixedArenaGC6
		; valid arena write barrier
		mov     [rcx], rdx
		ret

NotMixedArenaGC6:
        ; Do the move into the GC .  It is correct to take an AV here, the EH code
        ; figures out that this came from a WriteBarrier and correctly maps it back
        ; to the managed method which called the WriteBarrier (see setup in
        ; InitializeExceptionHandling, vm\exceptionhandling.cpp).
        mov     [rcx], rdx

		NOP ; padding for alignment of constant

PATCH_LABEL JIT_WriteBarrier_SVR64_PatchLabel_CardTable
        mov     rax, 0F0F0F0F0F0F0F0F0h

        shr     rcx, 0Bh

        cmp     byte ptr [rcx + rax], 0FFh
        jne     UpdateCardTable
        REPRET

    UpdateCardTable:
        mov     byte ptr [rcx + rax], 0FFh
        ret

MixedArenaGC6:
mov     [rcx], rdx

		mov     rcx, E_ACCESSDENIED ; access denied 
        add     rsp, MIN_SIZE
        ; void JIT_InternalThrow(unsigned exceptNum)
        jmp     JIT_InternalThrow
		ret
LEAF_END_MARKED JIT_WriteBarrier_SVR64, _TEXT

        end

