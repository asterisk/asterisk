#	
#	FreeAmp - The Free MP3 Player
#
#	Based on MP3 decoder originally Copyright (C) 1995-1997
#	Xing Technology Corp.  http://www.xingtech.com
#
#	Copyright (C) 1999 Mark H. Weaver <mhw@netris.org>
#
#	This program is free software; you can redistribute it and/or modify
#	it under the terms of the GNU General Public License as published by
#	the Free Software Foundation; either version 2 of the License, or
#	(at your option) any later version.
#
#	This program is distributed in the hope that it will be useful,
#	but WITHOUT ANY WARRANTY; without even the implied warranty of
#	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#	GNU General Public License for more details.
#
#	You should have received a copy of the GNU General Public License
#	along with this program; if not, write to the Free Software
#	Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
#	
#	$Id$
#

#%% extern wincoef,dword
#%% extern coef32,dword
#%% ! extern float wincoef[264];
#%% ! extern float coef32[31];

.equ L_tmp,	0
#%!.equ L_pcm,	4
#%% if-not-inline
.equ L_vbuf,	24
.equ L_vb_ptr,	28
.equ L_pcm,	32

.globl window_dual
	.align 16
#%% end-not-inline
#%% ! void window_dual(float *vbuf, int vb_ptr, short *pcm)
#%% ! {
window_dual:	#%% proc
#%% if-not-inline
	pushl %ebp
	pushl %edi
	pushl %esi
	pushl %ebx
	subl $4,%esp

	movl L_vb_ptr(%esp),%esi
	movl L_vbuf(%esp),%edi
#%% end-not-inline

#%!	movl vb_ptr,%esi
#%!	movl vbuf,%edi
#%!	movl pcm,%ecx
#%!	pushl %ebp
#%!	subl $8,%esp
#%!	movl %ecx,L_pcm(%esp)

	movl $511,%ebp		# ebp = 511
	leal wincoef,%ecx	# coef = wincoef
	addl $16,%esi		# si = vb_ptr + 16
	movl %esi,%ebx
	addl $32,%ebx
	andl %ebp,%ebx		# bx = (si + 32) & 511

# First 16
	movb $16,%dh		# i = 16
	.align 4
.FirstOuter:
	fldz			# sum = 0.0
	movb $2,%dl		# j = 2
	.align 4
.FirstInner:
.rept 4		# Unrolled loop
	flds (%ecx)		# Push *coef
	fmuls (%edi,%esi,4)	# Multiply by vbuf[si]
	addl $64,%esi		# si += 64
	addl $4,%ecx		# Advance coef pointer
	andl %ebp,%esi		# si &= 511
	faddp %st,%st(1)	# Add to sum
	
	flds (%ecx)		# Push *coef
	fmuls (%edi,%ebx,4)	# Multiply by vbuf[bx]
	addl $64,%ebx		# bx += 64
	addl $4,%ecx		# Advance coef pointer
	andl %ebp,%ebx		# bx &= 511
	fsubrp %st,%st(1)	# Subtract from sum
.endr

	decb %dl		# --j
	jg .FirstInner		# Jump back if j > 0

	fistpl L_tmp(%esp)	# tmp = (long) round (sum)
	incl %esi		# si++
	movl L_tmp(%esp),%eax
	decl %ebx		# bx--
	movl %eax,%ebp
	sarl $15,%eax
	incl %eax
	sarl $1,%eax
	jz .FirstInRange	# Jump if in range

	sarl $16,%eax		# Out of range
	movl $32767,%ebp
	xorl %eax,%ebp
.FirstInRange:
	movl L_pcm(%esp),%eax
	movw %bp,(%eax)		# Store sample in *pcm
	addl $4,%eax		# Increment pcm
	movl $511,%ebp		# Reload ebp with 511
	movl %eax,L_pcm(%esp)

	decb %dh		# --i
	jg .FirstOuter		# Jump back if i > 0


# Special case
	fldz			# sum = 0.0
	movb $4,%dl		# j = 4
	.align 4
.SpecialInner:
.rept 2		# Unrolled loop
	flds (%ecx)		# Push *coef
	fmuls (%edi,%ebx,4)	# Multiply by vbuf[bx]
	addl $64,%ebx		# bx += 64
	addl $4,%ecx		# Increment coef pointer
	andl %ebp,%ebx		# bx &= 511
	faddp %st,%st(1)	# Add to sum
.endr
	
	decb %dl		# --j
	jg .SpecialInner	# Jump back if j > 0

	fistpl L_tmp(%esp)	# tmp = (long) round (sum)
	decl %esi		# si--
	movl L_tmp(%esp),%eax
	incl %ebx		# bx++
	movl %eax,%ebp
	sarl $15,%eax
	incl %eax
	sarl $1,%eax
	jz .SpecialInRange	# Jump if within range

	sarl $16,%eax		# Out of range
	movl $32767,%ebp
	xorl %eax,%ebp
.SpecialInRange:
	movl L_pcm(%esp),%eax
	subl $36,%ecx		# Readjust coef pointer for last round
	movw %bp,(%eax)		# Store sample in *pcm
	addl $4,%eax		# Increment pcm
	movl $511,%ebp		# Reload ebp with 511
	movl %eax,L_pcm(%esp)


# Last 15
	movb $15,%dh		# i = 15
	.align 4
.LastOuter:
	fldz			# sum = 0.0
	movb $2,%dl		# j = 2
	.align 4
.LastInner:
.rept 4		# Unrolled loop
	flds (%ecx)		# Push *coef
	fmuls (%edi,%esi,4)	# Multiply by vbuf[si]
	addl $64,%esi		# si += 64
	subl $4,%ecx		# Back up coef pointer
	andl %ebp,%esi		# si &= 511
	faddp %st,%st(1)	# Add to sum
	
	flds (%ecx)		# Push *coef
	fmuls (%edi,%ebx,4)	# Multiply by vbuf[bx]
	addl $64,%ebx		# bx += 64
	subl $4,%ecx		# Back up coef pointer
	andl %ebp,%ebx		# bx &= 511
	faddp %st,%st(1)	# Add to sum
.endr

	decb %dl		# --j
	jg .LastInner		# Jump back if j > 0

	fistpl L_tmp(%esp)	# tmp = (long) round (sum)
	decl %esi		# si--
	movl L_tmp(%esp),%eax
	incl %ebx		# bx++
	movl %eax,%ebp
	sarl $15,%eax
	incl %eax
	sarl $1,%eax
	jz .LastInRange		# Jump if in range

	sarl $16,%eax		# Out of range
	movl $32767,%ebp
	xorl %eax,%ebp
.LastInRange:
	movl L_pcm(%esp),%eax
	movw %bp,(%eax)		# Store sample in *pcm
	addl $4,%eax		# Increment pcm
	movl $511,%ebp		# Reload ebp with 511
	movl %eax,L_pcm(%esp)

	decb %dh		# --i
	jg .LastOuter		# Jump back if i > 0

#%!	addl $8,%esp
#%!	popl %ebp

#%% if-not-inline
# Restore regs and return
	addl $4,%esp	
	popl %ebx
	popl %esi
	popl %edi
	popl %ebp
	ret
#%% end-not-inline
#%% endp
#%% ! }

#---------------------------------------------------------------------------

.equ L_mi,	0
.equ L_m,	4
.equ L_dummy,	8
#%!.equ L_in,	12
#%!.equ L_out,	16
#%!.equ L_buf,	20	# Temporary buffer
#%!.equ L_locals, 148	# Bytes used for locals
#%% if-not-inline
.equ L_buf,	12	# Temporary buffer
.equ L_in,	160
.equ L_out,	164
.equ L_locals,	140	# Bytes used for locals

.globl asm_fdct32
	.align 16
#%% end-not-inline
#%% ! void asm_fdct32(float in[], float out[])
#%% ! {
asm_fdct32:	#%% proc
#%% if-not-inline
	pushl %ebp
	pushl %edi
	pushl %esi
	pushl %ebx
	subl $L_locals,%esp

	movl L_in(%esp),%edi	# edi = x
	movl L_out(%esp),%esi	# esi = f
#%% end-not-inline

#%!	movl in,%edi		# edi = x
#%!	movl out,%esi		# esi = f
#%!	pushl %ebp
#%!	subl $L_locals,%esp

	leal coef32-128,%ecx	# coef = coef32 - (32 * 4)
	movl $1,4(%esp)		# m = 1
	movl $16,%ebp		# n = 32 / 2
	
	leal L_buf(%esp),%ebx
	movl %ebx,L_out(%esp)	# From now on, use temp buf instead of orig x
	jmp .ForwardLoopStart

	.align 4
.ForwardOuterLoop:
	movl L_in(%esp),%edi	# edi = x
	movl L_out(%esp),%esi	# esi = f
	movl %edi,L_out(%esp)	# Exchange mem versions of f/x for next iter
.ForwardLoopStart:
	movl %esi,L_in(%esp)
	movl L_m(%esp),%ebx	# ebx = m (temporarily)
	movl %ebx,L_mi(%esp)	# mi = m
	sall $1,%ebx		# Double m for next iter
	leal (%ecx,%ebp,8),%ecx	# coef += n * 8
	movl %ebx,L_m(%esp)	# Store doubled m
	leal (%esi,%ebp,4),%ebx	# ebx = f2 = f + n * 4
	sall $3,%ebp		# n *= 8

	.align 4
.ForwardMiddleLoop:
	movl %ebp,%eax		# q = n
	xorl %edx,%edx		# p = 0
	test $8,%eax
	jnz .ForwardInnerLoop1

	.align 4
.ForwardInnerLoop:
	subl $4,%eax		# q -= 4
	flds (%edi,%eax)	# push x[q]
	flds (%edi,%edx)	# push x[p]
	fld %st(1)		# Duplicate top two stack entries
	fld %st(1)
	faddp %st,%st(1)
	fstps (%esi,%edx)	# f[p] = x[p] + x[q]
	fsubp %st,%st(1)
	fmuls (%ecx,%edx)
	fstps (%ebx,%edx)	# f2[p] = coef[p] * (x[p] - x[q])
	addl $4,%edx		# p += 4

.ForwardInnerLoop1:
	subl $4,%eax		# q -= 4
	flds (%edi,%eax)	# push x[q]
	flds (%edi,%edx)	# push x[p]
	fld %st(1)		# Duplicate top two stack entries
	fld %st(1)
	faddp %st,%st(1)
	fstps (%esi,%edx)	# f[p] = x[p] + x[q]
	fsubp %st,%st(1)
	fmuls (%ecx,%edx)
	fstps (%ebx,%edx)	# f2[p] = coef[p] * (x[p] - x[q])
	addl $4,%edx		# p += 4

	cmpl %eax,%edx
	jb .ForwardInnerLoop	# Jump back if (p < q)

	addl %ebp,%esi		# f += n
	addl %ebp,%ebx		# f2 += n
	addl %ebp,%edi		# x += n
	decl L_mi(%esp)		# mi--
	jg .ForwardMiddleLoop	# Jump back if mi > 0

	sarl $4,%ebp		# n /= 16
	jg .ForwardOuterLoop	# Jump back if n > 0


# Setup back loop
	movl $8,%ebx		# ebx = m = 8 (temporarily)
	movl %ebx,%ebp		# n = 4 * 2

	.align 4
.BackOuterLoop:
	movl L_out(%esp),%esi	# esi = f
	movl %ebx,L_mi(%esp)	# mi = m
	movl L_in(%esp),%edi	# edi = x
	movl %ebx,L_m(%esp)	# Store m
	movl %esi,L_in(%esp)	# Exchange mem versions of f/x for next iter
	movl %edi,%ebx
	movl %edi,L_out(%esp)
	subl %ebp,%ebx		# ebx = x2 = x - n
	sall $1,%ebp		# n *= 2

	.align 4
.BackMiddleLoop:
	movl -4(%ebx,%ebp),%ecx
	movl %ecx,-8(%esi,%ebp)	# f[n - 8] = x2[n - 4]
	flds -4(%edi,%ebp)	# push x[n - 4]
	fsts -4(%esi,%ebp)	# f[n - 4] = x[n - 4], without popping
	leal -8(%ebp),%eax	# q = n - 8
	leal -16(%ebp),%edx	# p = n - 16

	.align 4
.BackInnerLoop:
	movl (%ebx,%eax),%ecx
	movl %ecx,(%esi,%edx)	# f[p] = x2[q]
	flds (%edi,%eax)	# push x[q]
	fadd %st,%st(1)
	fxch
	fstps 4(%esi,%edx)	# f[p + 4] = x[q] + x[q + 4]
	subl $4,%eax		# q -= 4
	subl $8,%edx		# p -= 8
	jge .BackInnerLoop	# Jump back if p >= 0

	fstps L_dummy(%esp)	# Pop (XXX is there a better way to do this?)
	addl %ebp,%esi		# f += n
	addl %ebp,%ebx		# x2 += n
	addl %ebp,%edi		# x += n
	decl L_mi(%esp)		# mi--
	jg .BackMiddleLoop	# Jump back if mi > 0

	movl L_m(%esp),%ebx	# ebx = m (temporarily)
	sarl $1,%ebx		# Halve m for next iter
	jg .BackOuterLoop	# Jump back if m > 0

#%!	addl $L_locals,%esp
#%!	popl %ebp

#%% if-not-inline
# Restore regs and return
	addl $L_locals,%esp
	popl %ebx
	popl %esi
	popl %edi
	popl %ebp
	ret
#%% end-not-inline
#%% endp
#%% ! }

