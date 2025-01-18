@localsize 8, 1, 1
@buf 8
@invocationid(r0.x)
@localmem 4
@branchstack 1

mov.u32u32 r1.x, 0

getone #l_all
mov.u32u32 r1.y, 5
(rpt2)nop
stlw.u32 l[r1.x], r1.y, 1

l_all:
(jp)ldlw.u32 r1.z, l[r1.x], 1
(ss)nop
stib.b.untyped.1d.u32.4.imm r1.z, r0.x, 0
end
