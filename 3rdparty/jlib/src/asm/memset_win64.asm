bits 64
default rel
%use smartalign
alignmode p6, 32

segment .text

global _jmem_set_asm_win64_ermsb
global _jmem_set_asm_win64

; https://msrc-blog.microsoft.com/2021/01/11/building-faster-amd64-memset-routines/
; void _jmem_set_asm_win64_ermsb(void* buffer, uint8_t ch, size_t sz)
_jmem_set_asm_win64_ermsb:
	; rcx = buffer;
	; rdx = ch
	; r8 = sz
	; Replicate 'ch' into all bytes of rdx
	movzx  edx, dl
	mov    r9, 0101010101010101h
	imul   rdx, r9
	movd   xmm0, edx
	pshufd xmm0, xmm0, 0

	; if (sz >= 800) goto memset_ermsb
	cmp r8, 800
	jae .memset_ermsb

	; if (sz >= 64) goto memset_64_799
	cmp r8, 64
	jae .memset_64_799

	; if (sz >= 16) goto memset_16_63
	cmp r8, 16
	jae .memset_16_63

	; if (sz >= 4) goto memset_4_15
	cmp r8, 4
	jae .memset_4_15

align 16
.memset_0_3:
	; Most uncommon case: 0 - 3 bytes
	; Store a single byte at a time by decrementing r8
	test r8, r8         ; if (sz == 0) 
	je .memset_0_3_done ;     return
	mov [rcx], dl       ; *(uint8_t*)dst = ch
	inc rcx             ; dst++
	dec r8              ; sz--
	je .memset_0_3_done ; if (sz == 0) return
	mov [rcx], dl       ; *(uint8_t*)dst = ch
	inc rcx             ; dst++
	dec r8              ; sz--
	je .memset_0_3_done ; if (sz == 0) return
	mov [rcx], dl       ; *(uint8_t*)dst = ch
.memset_0_3_done:
	ret                 ; return

align 16
.memset_64_799:
	movdqu [rcx], xmm0         ; *(__m128i*)dst = (__m128i){ ch, ..., ch }
	add    rcx, 16             ; dst += 16
	sub    r8, 16              ; sz -= 16
	cmp    r8, 64              ; if (sz < 64) 
	jb .memset_64_799_trailing ;     goto memset_64_799_trailing

	; Align dst to 16 bytes and do 64-byte store loop.
	mov rdx, rcx               ; Keep unaligned pointer to calculate how many bytes to add to counter
	and rcx, -16               ; Align pointer to 16 bytes
	sub rdx, rcx               ; Subtract unaligned from aligned pointer (>= 0) (alignment counter)
	mov r9, r8                 ; Calculate the number of 64-byte chunks
	shr r9, 6                  ;    r9 = sz / 64
	and r8, 63                 ; Calculate the number of trailing bytes
	add r8, rdx                ;    r8 = (sz & 63) + alignment counter
align 16
.loop_64:
	movdqa [rcx +  0], xmm0
	movdqa [rcx + 16], xmm0
	movdqa [rcx + 32], xmm0
	movdqa [rcx + 48], xmm0
	add    rcx, 64
	dec    r9
	jne .loop_64

.memset_64_799_trailing:
	lea    rdx, [rcx + r8 - 16] ; calculate where the last 16-byte store goes
	lea    r9, [rcx + r8 - 48]  ; calculate where the first "trailing bytes" store goes
	and    r9, -16              ; first "trailing bytes" store should be 16-byte aligned
	movdqa [r9 +  0], xmm0      ;
	movdqa [r9 + 16], xmm0      ;
	movdqa [r9 + 32], xmm0      ;
	movdqu [rdx + 0], xmm0      ;
	ret

align 16
.memset_16_63:
	; Set 16-63 bytes with 4 stores using variable size branchless stores
	lea    r9, [r8 + rcx - 16] ; compute the location of the last store, could be unaligned
	and    r8, 32              ; if size at least 32 bytes, r8 = 32, otherwise r8 = 0
	movdqu [rcx], xmm0         ; set the first 16 bytes
	shr    r8, 1               ; divide r8 by 2, this is now either 16 or 0
	movdqu [r9], xmm0          ; set the last location
	movdqu [rcx + r8], xmm0    ; set index 0 or index 16 based on if the size is at least 32
	neg    r8                  ; either 0 or -16
	movdqu [r9 + r8], xmm0     ; set the second to last location
	ret

align 16
.memset_4_15:
	; Set 4-15 bytes with 4 stores using variable size branchless stores
	lea r9, [r8 + rcx - 4]  ; compute the location of the last store, could be unaligned
	and r8, 8               ; if size at least 8 bytes, r8 = 8, otherwise r8 = 0
	mov [rcx], edx          ; set the first 4 bytes
	shr r8, 1               ; divide r8 by 2, this is now either 4 or 0
	mov [r9], edx           ; set the last location
	mov [rcx + r8], edx     ; set index 0 or index 4 based on if the size is at least 8
	neg r8                  ; either 0 or -4
	mov [r9 + r8], edx      ; set the second to last location
	ret

align 16
.memset_ermsb:
	; 64-byte store to potentially unaligned buffer
	movdqu [rcx +  0], xmm0
	movdqu [rcx + 16], xmm0
	movdqu [rcx + 32], xmm0
	movdqu [rcx + 48], xmm0

	; 64-byte align buffer and prepare for rep stosb
	push rdi              ; store rdi before using it
	lea  rdi, [rcx + 64]  ; 
	and  rdi, -64         ; rdi = aligned64 = (buffer + 64) & ~63
	sub  rcx, rdi         ;
	add  rcx, r8          ; rcx = sz - (aligned64 - buffer)
	mov  al, dl           ;
	rep  stosb            ; fill bytes
	pop  rdi              ; restore rdi
	ret

; Same logic as _jmem_set_asm_win64_ermsb but without the rep stosb part
_jmem_set_asm_win64:
	movzx edx, dl
	mov r9, 0101010101010101h
	imul rdx, r9
	movd xmm0, edx
	pshufd xmm0, xmm0, 0
	cmp r8, 64
	jae .memset_64_799
	cmp r8, 16
	jae .memset_16_63
	cmp r8, 4
	jae .memset_4_15
align 16
.memset_0_3:
	test r8, r8;
	je .memset_0_3_done;
	mov [rcx], dl;
	inc rcx;
	dec r8;
	je .memset_0_3_done;
	mov [rcx], dl;
	inc rcx;
	dec r8;
	je .memset_0_3_done;
	mov [rcx], dl;
.memset_0_3_done:
	ret
align 16
.memset_64_799:
	movdqu [rcx], xmm0
	add rcx, 16;
	sub r8, 16;
	cmp r8, 64;
	jb .memset_64_799_trailing
	mov rdx, rcx
	and rcx, -16
	sub rdx, rcx
	mov r9, r8
	shr r9, 6
	and r8, 63
	add r8, rdx
align 16
.loop_64:
	movdqa [rcx], xmm0
	movdqa [rcx + 16], xmm0
	movdqa [rcx + 32], xmm0
	movdqa [rcx + 48], xmm0
	add rcx, 64
	dec r9
	jne .loop_64
.memset_64_799_trailing:
	lea rdx, [rcx + r8 - 16]
	lea r9, [rcx + r8 - 48]
	and r9, -16
	movdqa [r9], xmm0
	movdqa [r9 + 16], xmm0
	movdqa [r9 + 32], xmm0
	movdqu [rdx], xmm0
	ret
align 16
.memset_16_63:
	lea r9, [r8 + rcx - 16]
	and r8, 32
	movdqu [rcx], xmm0
	shr r8, 1
	movdqu [r9], xmm0
	movdqu [rcx + r8], xmm0
	neg r8
	movdqu [r9 + r8], xmm0 
	ret
align 16
.memset_4_15:
	lea r9, [r8 + rcx - 4]
	and r8, 8
	mov [rcx], edx
	shr r8, 1
	mov [r9], edx
	mov [rcx + r8], edx
	neg r8
	mov [r9 + r8], edx
	ret
