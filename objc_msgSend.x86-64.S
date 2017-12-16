
.macro DTABLE next, idx
  movq  (\idx * 24 + 16)(%r11), %rbx
  cmp   (\idx * 24)(%r11), %rax
  jne   \next
  movq  (\idx * 24 + 8)(%r11), %r10
  cmpq  (\idx * 24 + 16)(%r11), %rbx
  jne   15f
  mov  -8(%rsp), %rax
  mov  -16(%rsp), %rbx
  jmp   *%r10
.endm

.macro MSGSEND receiver, sel
  .cfi_startproc                        # Start emitting unwind data.  We
                                        # don't actually care about any of
                                        # the stuff except the slow call,
                                        # because that's the only one that
                                        # can throw.

  test  \receiver, \receiver            # If the receiver is nil
  jz    4f                              # return nil
  movq  $SMALLOBJ_MASK, %r10            # Load the small object mask
  test  \receiver, %r10                 # Check if the receiver is a small object
  jnz   6f                              # Get the small object class

  mov   (\receiver), %r10               # Load the dtable from the class
1:                                        # classLoaded
  mov   (\sel), %r11                    # Load the selector index into %eax

  btr   $63, %r11
  jnc   5f

  mov   %rax, -8(%rsp)
  mov   %rbx, -16(%rsp)
  mov   %r10, %rax

#if INV_DTABLE_SIZE == 2
    DTABLE 21f, 0
21: DTABLE 15f, 1
#elif INV_DTABLE_SIZE == 4
    DTABLE 21f, 0
21: DTABLE 22f, 1
22: DTABLE 23f, 2
23: DTABLE 15f, 3
#elif INV_DTABLE_SIZE == 8
    DTABLE 21f, 0
21: DTABLE 22f, 1
22: DTABLE 23f, 2
23: DTABLE 24f, 3
24: DTABLE 25f, 4
25: DTABLE 26f, 5
26: DTABLE 27f, 6
27: DTABLE 15f, 7
#else
# error Invalid dtable size
#endif

7:
  jmp   *%r10
4:                                       # returnNil:
                                       # Both of the return registers are
                                       # callee-save on x86-64, so we can
                                       # return 0 in both in the same code:
  xor   %rax, %rax                     # Return 0 as an integer
  pxor  %xmm0, %xmm0                   # Return 0 as a floating point value
  ret
15:
  mov  -8(%rsp), %rax
  mov  -16(%rsp), %rbx
5:                                       # slowSend:
  push  %rax                           # We need to preserve all registers that may contain arguments:
  push  %rbx
  push  %rcx
  push  %r8
  push  %r9

  sub $0x98, %rsp
  movups  %xmm0, 0x80(%rsp)
  movups  %xmm1, 0x70(%rsp)
  movups  %xmm2, 0x60(%rsp)
  movups  %xmm3, 0x50(%rsp)
  movups  %xmm4, 0x40(%rsp)
  movups  %xmm5, 0x30(%rsp)
  movups  %xmm6, 0x20(%rsp)
  movups  %xmm7, 0x10(%rsp)

#rdi rsi rdx
  # We're (potentially) modifying the self argument with the lookup, so we don't want to be
.ifc "\receiver", "%rdi"
  push  %rdi
  mov   %rsp, %rdi
  push  %rsi                           # Save _cmd (not preserved across calls)
  push  %rdx
.else
  push  %rdi                           # Save the sret pointer
  push  %rsi                           # Save self where it can be modified
  mov   %rsp, %rdi
  push  %rdx
  mov   %rdx, %rsi                     # move _cmd to where the callee expects it to be
.endif

  .cfi_adjust_cfa_offset 0xD8
  call  CDECL(slowMsgLookup)           # Call the slow lookup function
  mov   %rax, %r10                     # Load the returned IMP

  pop   %rdx
  pop   %rsi
  pop   %rdi

  movups  0x80(%rsp), %xmm0
  movups  0x70(%rsp), %xmm1
  movups  0x60(%rsp), %xmm2
  movups  0x50(%rsp), %xmm3
  movups  0x40(%rsp), %xmm4
  movups  0x30(%rsp), %xmm5
  movups  0x20(%rsp), %xmm6
  movups  0x10(%rsp), %xmm7
  add   $0x98, %rsp

  pop   %r9
  pop   %r8
  pop   %rcx
  pop   %rbx
  pop   %rax
  jmp   7b
6:                                        # smallObject:
  and   \receiver, %r10                 # Find the small int type
  lea   CDECL(SmallObjectClasses)(%rip), %r11
  mov   (%r11, %r10, 8), %r10
  jmp   1b
  .cfi_endproc
.endm
.globl CDECL(objc_msgSend)
TYPE_DIRECTIVE(CDECL(objc_msgSend), @function)
.globl CDECL(objc_msgSend_fpret)
TYPE_DIRECTIVE(CDECL(objc_msgSend_fpret), @function)
CDECL(objc_msgSend_fpret):
CDECL(objc_msgSend):
  MSGSEND %rdi, %rsi
.globl CDECL(objc_msgSend_stret)
TYPE_DIRECTIVE(CDECL(objc_msgSend_stret), @function)
CDECL(objc_msgSend_stret):
  MSGSEND %rsi, %rdx
