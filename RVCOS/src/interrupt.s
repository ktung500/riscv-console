.section .text, "ax"
.global _interrupt_handler, enter_cartridge, call_th_ent, ContextSwitch, get_tp, set_tp, get_gp, hardware_interrupt, CallUpcall
.extern saved_sp

_interrupt_handler:
    csrw    mscratch,ra
    csrr    ra,mcause

    addi    ra,ra,-11              # the mcause will be 11 in the case of an ecall.
    bnez    ra,hardware_interrupt  # if the interrput is not = to 0, its not a syscall so go to hardware interrupt

    csrr    ra,mscratch            # otherwise it is a syscall, so go to syscall handler
    addi    sp,sp,-8
    sw      ra,4(sp)
    sw      gp,0(sp)
    .option push
    .option norelax
    la gp, __global_pointer$
    .option pop
    call    c_syscall_handler
    lw      ra,4(sp)
    lw      gp,0(sp)
    addi    sp,sp,8
    csrw    mepc,ra
    mret

hardware_interrupt:
   csrr ra,mscratch # puts mscratch into ra
   addi sp,sp,-48
   sw ra,44(sp)
   csrr ra, mepc  # puts mepc into ra
   sw gp,40(sp)
   sw ra,36(sp)
   sw t0,32(sp)
   sw t1,28(sp)
   sw t2,24(sp)
   sw a0,20(sp)
   sw a1,16(sp)
   sw a2,12(sp)
   sw a3,8(sp)
   sw a4,4(sp)
   sw a5,0(sp)
   .option push
   .option norelax
   la gp, __global_pointer$
   .option pop
   csrr a0, mcause
   call c_interrupt_handler
   #lw ra,44(sp)
   lw gp,40(sp)
   #mv t1, ra
   lw ra,36(sp)
   csrw mepc,ra
   #mv ra, t1
   lw ra,44(sp)
   lw t0,32(sp)
   lw t1,28(sp)
   lw t2,24(sp)
   lw a0,20(sp)
   lw a1,16(sp)
   lw a2,12(sp)
   lw a3,8(sp)
   lw a4,4(sp)
   lw a5,0(sp)
   addi sp,sp,48
   mret

enter_cartridge:
    addi    sp,sp,-12
    la      a5,saved_sp
    sw      sp,0(a5)
    sw      s0,8(sp)
    sw      s1,4(sp)
    sw      ra,0(sp)
    lui	    a5,0x40000
    addi	a5,a5,28
    lw	    a5,0(a5)
    andi	a5,a5,-4
    jalr	a5
    .option push
    .option norelax
    la gp, __global_pointer$
    .option pop
    la      a5,saved_sp
    lw      sp,0(a5)
    lw      s0,8(sp)
    lw      s1,4(sp)
    lw      ra,0(sp)
    addi    sp,sp,12
    ret

call_th_ent:
    addi    sp,sp,-4
    sw      ra, 0(sp)
    mv      gp, a2
    jalr    a1       # a1 should be the entry parameter
    .option push
    .option norelax
    la      gp, __global_pointer$
    .option pop
    lw      ra, 0(sp)
    addi    sp, sp, 4
    ret

ContextSwitch:
    addi    sp,sp,-52
    sw      ra,48(sp)
    sw      tp,44(sp)
    sw      t0,40(sp)
    sw      t1,36(sp)
    sw      t2,32(sp)
    sw      s0,28(sp)
    sw      s1,24(sp)
    sw      a0,20(sp)
    sw      a1,16(sp)
    sw      a2,12(sp)
    sw      a3,8(sp)
    sw      a4,4(sp)
    sw      a5,0(sp)
    sw      sp,0(a0) #a0
    mv      sp,a1
    lw      ra,48(sp)
    lw      tp,44(sp)
    lw      t0,40(sp)
    lw      t1,36(sp)
    lw      t2,32(sp)
    lw      s0,28(sp)
    lw      s1,24(sp)
    lw      a0,20(sp)
    lw      a1,16(sp)
    lw      a2,12(sp)
    lw      a3,8(sp)
    lw      a4,4(sp)
    lw      a5,0(sp)
    addi    sp,sp,52
    ret

get_tp:
    mv    a0,tp
    ret

set_tp:
    mv tp,a0
    ret

get_gp:
    mv  a0, gp
    ret

CallUpcall:
    addi    sp,sp,-40
    sw	    ra,36(sp)
    sw	    t0,32(sp)
    sw	    t1,28(sp)
    sw	    t2,24(sp)
    sw	    a0,20(sp)
    sw	    a1,16(sp)
    sw	    a2,12(sp)
    sw	    a3,8(sp)
    sw	    a4,4(sp)
    sw	    a5,0(sp)
    addi    a3,a3,-4
    sw      sp,0(a3)
    mv      sp,a3
    mv      gp,a2
    csrsi mstatus, 0x8 # enable interrupts
    jalr    a1
    csrci mstatus, 0x8 # disable interrupts
    lw      sp,0(sp)
    .option push
    .option norelax
    la gp, __global_pointer$
    .option pop
    lw	    ra,36(sp)
    lw	    t0,32(sp)
    lw	    t1,28(sp)
    lw	    t2,24(sp)
    lw	    a0,20(sp)
    lw	    a1,16(sp)
    lw	    a2,12(sp)
    lw	    a3,8(sp)
    lw	    a4,4(sp)
    lw	    a5,0(sp)
    addi    sp,sp,40
    ret