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
ArenaMarshal equ ?ArenaMarshal@ArenaManager@@SAXPEAX0@Z
extern ArenaMarshal:proc
MIN_SIZE equ 28h


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
NESTED_ENTRY JIT_WriteBarrier_PreGrow32, _TEXT
        align 4
        ; Do the move into the GC .  It is correct to take an AV here, the EH code
        ; figures out that this came from a WriteBarrier and correctly maps it back
        ; to the managed method which called the WriteBarrier (see setup in
        ; InitializeExceptionHandling, vm\exceptionhandling.cpp).
		bt      rcx,42
		jnc     targetNotArena2
		bt      rdx,42
		jnc      marshal2

		; both arena - check if same buffer
		mov     rax,rdx
		xor     rax,rcx
		shr     rax,20
		and     rax,3fffffh
		je      nomarshalarena2

		; check if buffers come from same arena
		mov     rax,rdx
		shr     rax,20
		and     rax,3fffffh
		mov     r8d,1
		shl     r8,42
		push	rbx
		mov     bx,[r8+2*rax]
		mov     rax,rcx
		shr     rax,20
		and     rax,3fffffh
		cmp     bx,[r8+2*rax]
		pop		rbx
		jne     marshal2

nomarshalarena2:
		mov     [rcx],rdx
		ret
marshal2:
        jmp     lmarshal2

targetNotArena2:  
		bt		rdx,42
		jc		marshal2
        mov     [rcx], rdx

        nop ; padding for alignment of constant
        nop ; padding for alignment of constant
        nop ; padding for alignment of constant

card2:
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
	lmarshal2:
        PUSH_CALLEE_SAVED_REGISTERS

        alloc_stack         20h

        END_PROLOGUE
    
        mov                 rax, ArenaMarshal
		call                rax

        add                 rsp, 20h
        POP_CALLEE_SAVED_REGISTERS
		ret

    align 16
    Exit:
        REPRET
NESTED_END_MARKED JIT_WriteBarrier_PreGrow32, _TEXT


NESTED_ENTRY JIT_WriteBarrier_PreGrow64, _TEXT
        align 8
        ; Do the move into the GC .  It is correct to take an AV here, the EH code
        ; figures out that this came from a WriteBarrier and correctly maps it back
        ; to the managed method which called the WriteBarrier (see setup in
        ; InitializeExceptionHandling, vm\exceptionhandling.cpp).
		bt      rcx,42
		jnc     targetNotArena3
		bt      rdx,42
		jnc      marshal3

		; both arena - check if same buffer
		mov     rax,rdx
		xor     rax,rcx
		shr     rax,20
		and     rax,3fffffh
		je      nomarshalarena3

		; check if buffers come from same arena
		mov     rax,rdx
		shr     rax,20
		and     rax,3fffffh
		mov     r8d,1
		shl     r8,42
		push	rbx
		mov     bx,[r8+2*rax]
		mov     rax,rcx
		shr     rax,20
		and     rax,3fffffh
		cmp     bx,[r8+2*rax]
		pop		rbx
		jne     marshal3

nomarshalarena3:
		mov     [rcx],rdx
		ret
marshal3:
        jmp     lmarshal3

targetNotArena3:  
		bt		rdx,42
		jc		marshal3
        mov     [rcx], rdx
		
		nop
		nop
		nop
		nop

card3:
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
lmarshal3:		
        PUSH_CALLEE_SAVED_REGISTERS

        alloc_stack         20h

        END_PROLOGUE
    
        mov                 rax, ArenaMarshal
		bt rsp,3
		jc odd3
		
		call                rax
        jmp done3
	odd3:
		push rax
		call rax
		pop rax
	done3:
        add                 rsp, 20h
        POP_CALLEE_SAVED_REGISTERS
		ret

    align 16
    Exit:
        REPRET
NESTED_END_MARKED JIT_WriteBarrier_PreGrow64, _TEXT


; See comments for JIT_WriteBarrier_PreGrow (above).
NESTED_ENTRY JIT_WriteBarrier_PostGrow64, _TEXT
        align 8
		bt      rcx,42
		jnc     targetNotArena4
		bt      rdx,42
		jnc      marshal4

		; both arena - check if same buffer
		mov     rax,rdx
		xor     rax,rcx
		shr     rax,20
		and     rax,3fffffh
		je      nomarshalarena4

		; check if buffers come from same arena

		mov     rax,rdx
		shr     rax,20
		and     rax,3fffffh
		mov     r8d,1
		shl     r8,42
		push	rbx
		mov     bx,[r8+2*rax]
		mov     rax,rcx
		shr     rax,20
		and     rax,3fffffh
		cmp     bx,[r8+2*rax]
		pop		rbx
		jne     marshal4
nomarshalarena4:
		mov     [rcx],rdx
		ret
marshal4:
        jmp     lmarshal4

targetNotArena4:  
		bt		rdx,42
		jc		marshal4
        ; Do the move into the GC .  It is correct to take an AV here, the EH code
        ; figures out that this came from a WriteBarrier and correctly maps it back
        ; to the managed method which called the WriteBarrier (see setup in
        ; InitializeExceptionHandling, vm\exceptionhandling.cpp).
        mov     [rcx], rdx

		NOP ; padding for alignment of constant
		NOP ; padding for alignment of constant
        nop ; padding for alignment of constant
        nop ; padding for alignment of constant
 
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

	lmarshal4:
		mov rbx,rcx
        PUSH_CALLEE_SAVED_REGISTERS

        alloc_stack         20h

        END_PROLOGUE
    
		mov                 rax,ArenaMarshal
		bt rsp,3
		jc odd4
		
		call                rax
		jmp done4
	odd4:
		push rax
		call rax
		pop rax
	done4:
        add                 rsp, 20h
        POP_CALLEE_SAVED_REGISTERS
		ret


    align 16
    Exit:
        REPRET
NESTED_END_MARKED JIT_WriteBarrier_PostGrow64, _TEXT


NESTED_ENTRY JIT_WriteBarrier_PostGrow32, _TEXT
        align 4
		bt      rcx,42
		jnc     targetNotArena5
		bt      rdx,42
		jnc      marshal5

		; both arena - check if same buffer
		mov     rax,rdx
		xor     rax,rcx
		shr     rax,20
		and     rax,3fffffh
		je      nomarshalarena5

		; check if buffers come from same arena
		mov     rax,rdx
		shr     rax,20
		and     rax,3fffffh
		mov     r8d,1
		shl     r8,42
		push	rbx
		mov     bx,[r8+2*rax]
		mov     rax,rcx
		shr     rax,20
		and     rax,3fffffh
		cmp     bx,[r8+2*rax]
		pop		rbx
		jne     marshal5
nomarshalarena5:
		mov     [rcx],rdx
		ret
marshal5:
        jmp     lmarshal5

targetNotArena5:  
		bt		rdx,42
		jc		marshal5
        ; Do the move into the GC .  It is correct to take an AV here, the EH code
        ; figures out that this came from a WriteBarrier and correctly maps it back
        ; to the managed method which called the WriteBarrier (see setup in
        ; InitializeExceptionHandling, vm\exceptionhandling.cpp).
        mov     [rcx], rdx

        nop ; padding for alignment of constant
        nop ; padding for alignment of constant
        nop ; padding for alignment of constant

        ; Check the lower and upper ephemeral region bounds
card5:
PATCH_LABEL JIT_WriteBarrier_PostGrow32_PatchLabel_Lower
        cmp     rdx, 0F0F0F0F0h
        jb      Exit

        nop ; padding for alignment of constant
        nop ; padding for alignment of constant
        nop ; padding for alignment of constant

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
lmarshal5:
        PUSH_CALLEE_SAVED_REGISTERS

        alloc_stack         20h

        END_PROLOGUE
    
        mov                 rax, ArenaMarshal
		call                rax

        add                 rsp, 20h
        POP_CALLEE_SAVED_REGISTERS
		ret


    align 16
    Exit:
        REPRET
NESTED_END_MARKED JIT_WriteBarrier_PostGrow32, _TEXT



NESTED_ENTRY JIT_WriteBarrier_SVR32, _TEXT
        align 4
        ;
        ; SVR GC has multiple heaps, so it cannot provide one single 
        ; ephemeral region to bounds check against, so we just skip the
        ; bounds checking all together and do our card table update 
        ; unconditionally.
        ;

		bt      rcx,42
		jnc     targetNotArena6
		bt      rdx,42
		jnc      marshal6

		; both arena - check if same buffer
		mov     rax,rdx
		xor     rax,rcx
		shr     rax,20
		and     rax,3fffffh
		je      nomarshalarena6

		; check if buffers come from same arena
		mov     rax,rdx
		shr     rax,20
		and     rax,3fffffh
		mov     r8d,1
		shl     r8,42
		push	rbx
		mov     bx,[r8+2*rax]
		mov     rax,rcx
		shr     rax,20
		and     rax,3fffffh
		cmp     bx,[r8+2*rax]
		pop		rbx
		jne     marshal6
nomarshalarena6:
		mov     [rcx],rdx
		ret
marshal6:
        jmp     lmarshal6

targetNotArena6:  
		bt		rdx,42
		jc		marshal6
        ; Do the move into the GC .  It is correct to take an AV here, the EH code
        ; figures out that this came from a WriteBarrier and correctly maps it back
        ; to the managed method which called the WriteBarrier (see setup in
        ; InitializeExceptionHandling, vm\exceptionhandling.cpp).
        mov     [rcx], rdx

        shr     rcx, 0Bh

card6:
PATCH_LABEL JIT_WriteBarrier_SVR32_PatchLabel_CheckCardTable
        cmp     byte ptr [rcx + 0F0F0F0F0h], 0FFh
        jne     UpdateCardTable
        REPRET

        nop ; padding for alignment of constant

PATCH_LABEL JIT_WriteBarrier_SVR32_PatchLabel_UpdateCardTable
    UpdateCardTable:
        mov     byte ptr [rcx + 0F0F0F0F0h], 0FFh
        ret

lmarshal6:
        PUSH_CALLEE_SAVED_REGISTERS

        alloc_stack         20h

        END_PROLOGUE
    
        mov                 rax, ArenaMarshal
		call                rax

        add                 rsp, 20h
        POP_CALLEE_SAVED_REGISTERS
		ret


NESTED_END_MARKED JIT_WriteBarrier_SVR32, _TEXT

NESTED_ENTRY JIT_WriteBarrier_SVR64, _TEXT
        align 8
        ;
        ; SVR GC has multiple heaps, so it cannot provide one single 
        ; ephemeral region to bounds check against, so we just skip the
        ; bounds checking all together and do our card table update 
        ; unconditionally.
        ;
		bt      rcx,42
		jnc     targetNotArena7
		bt      rdx,42
		jnc      marshal7

		; both arena - check if same buffer
		mov     rax,rdx
		xor     rax,rcx
		shr     rax,20
		and     rax,3fffffh
		je      nomarshalarena7

		; check if buffers come from same arena
		mov     rax,rdx
		shr     rax,20
		and     rax,3fffffh
		mov     r8d,1
		shl     r8,42
		push	rbx
		mov     bx,[r8+2*rax]
		mov     rax,rcx
		shr     rax,20
		and     rax,3fffffh
		cmp     bx,[r8+2*rax]
		pop		rbx
		jne     marshal7
nomarshalarena7:
		mov     [rcx],rdx
		ret
marshal7:
        jmp     lmarshal7

targetNotArena7:  
		bt		rdx,42
		jc		marshal7
        ; Do the move into the GC .  It is correct to take an AV here, the EH code
        ; figures out that this came from a WriteBarrier and correctly maps it back
        ; to the managed method which called the WriteBarrier (see setup in
        ; InitializeExceptionHandling, vm\exceptionhandling.cpp).
        mov     [rcx], rdx

		nop
		nop
		nop
		nop

card7:
PATCH_LABEL JIT_WriteBarrier_SVR64_PatchLabel_CardTable
        mov     rax, 0F0F0F0F0F0F0F0F0h

        shr     rcx, 0Bh
		nop
		nop

        cmp     byte ptr [rcx + rax], 0FFh
        jne     UpdateCardTable
        REPRET

    UpdateCardTable:
        mov     byte ptr [rcx + rax], 0FFh
        ret

lmarshal7:
        PUSH_CALLEE_SAVED_REGISTERS

        alloc_stack         20h

        END_PROLOGUE
    
        mov                 rax, ArenaMarshal
		bt rsp,3
		jc odd7
		
		call                rax
        jmp done7
	odd7:
		push rax
		call rax
		pop rax
	done7:
        add                 rsp, 20h
        POP_CALLEE_SAVED_REGISTERS
		ret

NESTED_END_MARKED JIT_WriteBarrier_SVR64, _TEXT


        end

