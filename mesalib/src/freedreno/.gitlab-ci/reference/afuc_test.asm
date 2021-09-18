; a6xx microcode
; Disassembling microcode: src/freedreno/.gitlab-ci/reference/afuc_test.fw
; Version: 01000001

        [01000001]  ; nop
        [01000078]  ; nop
        mov $01, 0x0830	; CP_SQE_INSTR_BASE
        mov $02, 0x0002
        cwrite $01, [$00 + @REG_READ_ADDR], 0x0
        cwrite $02, [$00 + @REG_READ_DWORDS], 0x0
        mov $01, $regdata
        mov $02, $regdata
        add $01, $01, 0x0004
        addhi $02, $02, 0x0000
        mov $03, 0x0001
        cwrite $01, [$00 + @MEM_READ_ADDR], 0x0
        cwrite $02, [$00 + @MEM_READ_ADDR+0x1], 0x0
        cwrite $03, [$00 + @MEM_READ_DWORDS], 0x0
        rot $04, $memdata, 0x0008
        ushr $04, $04, 0x0006
        sub $04, $04, 0x0004
        add $01, $01, $04
        addhi $02, $02, 0x0000
        mov $rem, 0x0080
        cwrite $01, [$00 + @MEM_READ_ADDR], 0x0
        cwrite $02, [$00 + @MEM_READ_ADDR+0x1], 0x0
        cwrite $02, [$00 + @LOAD_STORE_HI], 0x0
        cwrite $rem, [$00 + @MEM_READ_DWORDS], 0x0
        cwrite $00, [$00 + @PACKET_TABLE_WRITE_ADDR], 0x0
        (rep)cwrite $memdata, [$00 + @PACKET_TABLE_WRITE], 0x0
        mov $02, 0x0883	; CP_SCRATCH[0].REG
        mov $03, 0xbeef
        mov $04, 0xdead << 16
        or $03, $03, $04
        cwrite $02, [$00 + @REG_WRITE_ADDR], 0x0
        cwrite $03, [$00 + @REG_WRITE], 0x0
        waitin
        mov $01, $data

CP_ME_INIT:
        mov $02, 0x0002
        waitin
        mov $01, $data

CP_MEM_WRITE:
        mov $addr, 0x00a0 << 24	; |NRT_ADDR
        mov $02, 0x0004
        (xmov1)add $data, $02, $data
        mov $addr, 0xa204 << 16	; |NRT_DATA
        (rep)(xmov3)mov $data, $data
        waitin
        mov $01, $data

CP_SCRATCH_WRITE:
        mov $02, 0x00ff
        (rep)cwrite $data, [$02 + 0x001], 0x4
        waitin
        mov $01, $data

CP_SET_SECURE_MODE:
        mov $02, $data
        setsecure $02, #l000
 l001:  jump #l001
        nop
 l000:  waitin
        mov $01, $data
fxn00:
 l004:  cmp $04, $02, $03
        breq $04, b0, #l002
        brne $04, b1, #l003
        breq $04, b2, #l004
        sub $03, $03, $02
 l003:  jump #l004
        sub $02, $02, $03
 l002:  ret
        nop

CP_REG_RMW:
        cwrite $data, [$00 + @REG_READ_ADDR], 0x0
        add $02, $regdata, 0x0042
        addhi $03, $00, $regdata
        sub $02, $02, $regdata
        call #fxn00
        subhi $03, $03, $regdata
        and $02, $02, $regdata
        or $02, $02, 0x0001
        xor $02, $02, 0x0001
        not $02, $02
        shl $02, $02, $regdata
        ushr $02, $02, $regdata
        ishr $02, $02, $regdata
        rot $02, $02, $regdata
        min $02, $02, $regdata
        max $02, $02, $regdata
        mul8 $02, $02, $regdata
        msb $02, $02
        mov $usraddr, $data
        mov $data, $02
        waitin
        mov $01, $data

CP_MEMCPY:
        mov $02, $data
        mov $03, $data
        mov $04, $data
        mov $05, $data
        mov $06, $data
 l006:  breq $06, 0x0, #l005
        cwrite $03, [$00 + @LOAD_STORE_HI], 0x0
        load $07, [$02 + 0x004], 0x4
        cwrite $05, [$00 + @LOAD_STORE_HI], 0x0
        jump #l006
        store $07, [$04 + 0x004], 0x4
 l005:  waitin
        mov $01, $data

CP_MEM_TO_MEM:
        cwrite $data, [$00 + @MEM_READ_ADDR], 0x0
        cwrite $data, [$00 + @MEM_READ_ADDR+0x1], 0x0
        mov $02, $data
        cwrite $data, [$00 + @LOAD_STORE_HI], 0x0
        mov $rem, $data
        cwrite $rem, [$00 + @MEM_READ_DWORDS], 0x0
        (rep)store $memdata, [$02 + 0x004], 0x4
        waitin
        mov $01, $data

UNKN15:
        cread $02, [$00 + 0x101], 0x0
        brne $02, 0x1, #l007
        nop
        preemptleave #l001
        nop
        nop
        nop
        waitin
        mov $01, $data
 l007:  iret
        nop

UNKN0:
UNKN1:
UNKN2:
UNKN3:
PKT4:
UNKN5:
UNKN6:
UNKN7:
UNKN8:
UNKN9:
UNKN10:
UNKN11:
UNKN12:
UNKN13:
UNKN14:
CP_NOP:
CP_RECORD_PFP_TIMESTAMP:
CP_WAIT_MEM_WRITES:
CP_WAIT_FOR_ME:
CP_WAIT_MEM_GTE:
UNKN21:
UNKN22:
UNKN23:
UNKN24:
CP_DRAW_PRED_ENABLE_GLOBAL:
CP_DRAW_PRED_ENABLE_LOCAL:
UNKN27:
CP_PREEMPT_ENABLE:
CP_SKIP_IB2_ENABLE_GLOBAL:
CP_PREEMPT_TOKEN:
UNKN31:
UNKN32:
CP_DRAW_INDX:
CP_SKIP_IB2_ENABLE_LOCAL:
CP_DRAW_AUTO:
CP_SET_STATE:
CP_WAIT_FOR_IDLE:
CP_IM_LOAD:
CP_DRAW_INDIRECT:
CP_DRAW_INDX_INDIRECT:
CP_DRAW_INDIRECT_MULTI:
CP_IM_LOAD_IMMEDIATE:
CP_BLIT:
CP_SET_CONSTANT:
CP_SET_BIN_DATA5_OFFSET:
CP_SET_BIN_DATA5:
UNKN48:
CP_RUN_OPENCL:
CP_LOAD_STATE6_GEOM:
CP_EXEC_CS:
CP_LOAD_STATE6_FRAG:
CP_SET_SUBDRAW_SIZE:
CP_LOAD_STATE6:
CP_INDIRECT_BUFFER_PFD:
CP_DRAW_INDX_OFFSET:
CP_REG_TEST:
CP_COND_INDIRECT_BUFFER_PFE:
CP_INVALIDATE_STATE:
CP_WAIT_REG_MEM:
CP_REG_TO_MEM:
CP_INDIRECT_BUFFER:
CP_INTERRUPT:
CP_EXEC_CS_INDIRECT:
CP_MEM_TO_REG:
CP_SET_DRAW_STATE:
CP_COND_EXEC:
CP_COND_WRITE5:
CP_EVENT_WRITE:
CP_COND_REG_EXEC:
UNKN73:
CP_REG_TO_SCRATCH:
CP_SET_DRAW_INIT_FLAGS:
CP_SCRATCH_TO_REG:
CP_DRAW_PRED_SET:
CP_MEM_WRITE_CNTR:
CP_START_BIN:
CP_END_BIN:
CP_WAIT_REG_EQ:
CP_SMMU_TABLE_UPDATE:
UNKN84:
CP_SET_CTXSWITCH_IB:
CP_SET_PSEUDO_REG:
CP_INDIRECT_BUFFER_CHAIN:
CP_EVENT_WRITE_SHD:
CP_EVENT_WRITE_CFL:
UNKN90:
CP_EVENT_WRITE_ZPD:
CP_CONTEXT_REG_BUNCH:
CP_WAIT_IB_PFD_COMPLETE:
CP_CONTEXT_UPDATE:
CP_SET_PROTECTED_MODE:
UNKN96:
UNKN97:
CP_WHERE_AM_I:
CP_SET_MODE:
CP_SET_VISIBILITY_OVERRIDE:
CP_SET_MARKER:
UNKN103:
UNKN104:
UNKN105:
UNKN106:
UNKN107:
UNKN108:
CP_REG_WRITE:
UNKN110:
CP_BOOTSTRAP_UCODE:
CP_WAIT_TWO_REGS:
CP_TEST_TWO_MEMS:
CP_REG_TO_MEM_OFFSET_REG:
CP_REG_TO_MEM_OFFSET_MEM:
UNKN118:
UNKN119:
CP_REG_WR_NO_CTXT:
UNKN121:
UNKN122:
UNKN123:
UNKN124:
UNKN125:
UNKN126:
UNKN127:
        waitin
        mov $01, $data
        [00000076]  ; nop
        [00000076]  ; nop
        [00000076]  ; nop
        [00000076]  ; nop
        [00000076]  ; nop
        [00000076]  ; nop
        [00000076]  ; nop
        [00000076]  ; nop
        [00000076]  ; nop
        [00000076]  ; nop
        [00000076]  ; nop
        [00000076]  ; nop
        [00000076]  ; nop
        [00000076]  ; nop
        [00000076]  ; nop
        [0000006b]  ; nop
        [00000076]  ; nop
        [00000076]  ; nop
        [00000076]  ; nop
        [00000076]  ; nop
        [00000076]  ; nop
        [00000076]  ; nop
        [00000076]  ; nop
        [00000076]  ; nop
        [00000076]  ; nop
        [00000076]  ; nop
        [00000076]  ; nop
        [00000076]  ; nop
        [00000076]  ; nop
        [00000076]  ; nop
        [00000076]  ; nop
        [00000076]  ; nop
        [00000076]  ; nop
        [0000003f]  ; nop
        [00000076]  ; nop
        [00000076]  ; nop
        [00000076]  ; nop
        [00000076]  ; nop
        [00000076]  ; nop
        [00000076]  ; nop
        [00000076]  ; nop
        [00000076]  ; nop
        [00000076]  ; nop
        [00000076]  ; nop
        [00000076]  ; nop
        [00000076]  ; nop
        [00000076]  ; nop
        [00000076]  ; nop
        [00000076]  ; nop
        [00000076]  ; nop
        [00000076]  ; nop
        [00000076]  ; nop
        [00000076]  ; nop
        [00000076]  ; nop
        [00000076]  ; nop
        [00000076]  ; nop
        [00000076]  ; nop
        [00000076]  ; nop
        [00000076]  ; nop
        [00000076]  ; nop
        [00000076]  ; nop
        [00000025]  ; nop
        [00000076]  ; nop
        [00000076]  ; nop
        [00000076]  ; nop
        [00000076]  ; nop
        [00000076]  ; nop
        [00000076]  ; nop
        [00000076]  ; nop
        [00000076]  ; nop
        [00000076]  ; nop
        [00000076]  ; nop
        [00000022]  ; nop
        [00000076]  ; nop
        [00000076]  ; nop
        [00000076]  ; nop
        [0000002c]  ; nop
        [00000076]  ; nop
        [00000076]  ; nop
        [00000076]  ; nop
        [00000076]  ; nop
        [00000076]  ; nop
        [00000076]  ; nop
        [00000076]  ; nop
        [00000076]  ; nop
        [00000076]  ; nop
        [00000076]  ; nop
        [00000076]  ; nop
        [00000076]  ; nop
        [00000076]  ; nop
        [00000076]  ; nop
        [00000076]  ; nop
        [00000076]  ; nop
        [00000076]  ; nop
        [00000076]  ; nop
        [00000076]  ; nop
        [00000076]  ; nop
        [00000076]  ; nop
        [00000076]  ; nop
        [00000076]  ; nop
        [00000076]  ; nop
        [00000076]  ; nop
        [00000030]  ; nop
        [00000076]  ; nop
        [00000076]  ; nop
        [00000076]  ; nop
        [00000076]  ; nop
        [00000076]  ; nop
        [00000076]  ; nop
        [00000076]  ; nop
        [00000076]  ; nop
        [00000076]  ; nop
        [00000076]  ; nop
        [00000076]  ; nop
        [00000076]  ; nop
        [00000062]  ; nop
        [00000076]  ; nop
        [00000055]  ; nop
        [00000076]  ; nop
        [00000076]  ; nop
        [00000076]  ; nop
        [00000076]  ; nop
        [00000076]  ; nop
        [00000076]  ; nop
        [00000076]  ; nop
        [00000076]  ; nop
        [00000076]  ; nop
        [00000076]  ; nop
