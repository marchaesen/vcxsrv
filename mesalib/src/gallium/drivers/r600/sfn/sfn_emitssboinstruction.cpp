#include "sfn_emitssboinstruction.h"

#include "sfn_instruction_fetch.h"
#include "sfn_instruction_gds.h"
#include "sfn_instruction_misc.h"
#include "sfn_instruction_tex.h"
#include "../r600_pipe.h"
#include "../r600_asm.h"

namespace r600 {

#define R600_SHADER_BUFFER_INFO_SEL (512 + R600_BUFFER_INFO_OFFSET / 16)

EmitSSBOInstruction::EmitSSBOInstruction(ShaderFromNirProcessor& processor):
   EmitInstruction(processor),
   m_require_rat_return_address(false),
   m_ssbo_image_offset(0)
{
}

void EmitSSBOInstruction::set_ssbo_offset(int offset)
{
   m_ssbo_image_offset = offset;
}


void EmitSSBOInstruction::set_require_rat_return_address()
{
   m_require_rat_return_address = true;
}

bool
EmitSSBOInstruction::load_rat_return_address()
{
   if (m_require_rat_return_address) {
      m_rat_return_address = get_temp_vec4();
      emit_instruction(new AluInstruction(op1_mbcnt_32lo_accum_prev_int, m_rat_return_address.reg_i(0), literal(-1), {alu_write}));
      emit_instruction(new AluInstruction(op1_mbcnt_32hi_int, m_rat_return_address.reg_i(1), literal(-1), {alu_write}));
      emit_instruction(new AluInstruction(op3_muladd_uint24, m_rat_return_address.reg_i(2), PValue(new InlineConstValue(ALU_SRC_SE_ID, 0)),
                                          literal(256), PValue(new InlineConstValue(ALU_SRC_HW_WAVE_ID, 0)), {alu_write, alu_last_instr}));
      emit_instruction(new AluInstruction(op3_muladd_uint24, m_rat_return_address.reg_i(1),
                                          m_rat_return_address.reg_i(2), literal(0x40), m_rat_return_address.reg_i(0),
      {alu_write, alu_last_instr}));
      m_require_rat_return_address = false;
   }
   return true;
}


bool EmitSSBOInstruction::do_emit(nir_instr* instr)
{
   const nir_intrinsic_instr *intr = nir_instr_as_intrinsic(instr);
   switch (intr->intrinsic) {
   case nir_intrinsic_atomic_counter_add:
   case nir_intrinsic_atomic_counter_and:
   case nir_intrinsic_atomic_counter_exchange:
   case nir_intrinsic_atomic_counter_max:
   case nir_intrinsic_atomic_counter_min:
   case nir_intrinsic_atomic_counter_or:
   case nir_intrinsic_atomic_counter_xor:
   case nir_intrinsic_atomic_counter_comp_swap:
      return emit_atomic(intr);
   case nir_intrinsic_atomic_counter_read:
   case nir_intrinsic_atomic_counter_post_dec:
      return emit_unary_atomic(intr);
   case nir_intrinsic_atomic_counter_inc:
      return emit_atomic_inc(intr);
   case nir_intrinsic_atomic_counter_pre_dec:
      return emit_atomic_pre_dec(intr);
   case nir_intrinsic_load_ssbo:
       return emit_load_ssbo(intr);
   case nir_intrinsic_store_ssbo:
      return emit_store_ssbo(intr);
   case nir_intrinsic_ssbo_atomic_add:
   case nir_intrinsic_ssbo_atomic_comp_swap:
   case nir_intrinsic_ssbo_atomic_or:
   case nir_intrinsic_ssbo_atomic_xor:
   case nir_intrinsic_ssbo_atomic_imax:
   case nir_intrinsic_ssbo_atomic_imin:
   case nir_intrinsic_ssbo_atomic_umax:
   case nir_intrinsic_ssbo_atomic_umin:
   case nir_intrinsic_ssbo_atomic_and:
   case nir_intrinsic_ssbo_atomic_exchange:
      return emit_ssbo_atomic_op(intr);
   case nir_intrinsic_image_store:
      return emit_image_store(intr);
   case nir_intrinsic_image_load:
   case nir_intrinsic_image_atomic_add:
   case nir_intrinsic_image_atomic_and:
   case nir_intrinsic_image_atomic_or:
   case nir_intrinsic_image_atomic_xor:
   case nir_intrinsic_image_atomic_exchange:
   case nir_intrinsic_image_atomic_comp_swap:
   case nir_intrinsic_image_atomic_umin:
   case nir_intrinsic_image_atomic_umax:
   case nir_intrinsic_image_atomic_imin:
   case nir_intrinsic_image_atomic_imax:
      return emit_image_load(intr);
   case nir_intrinsic_image_size:
      return emit_image_size(intr);
   case nir_intrinsic_get_ssbo_size:
      return emit_buffer_size(intr);
   case nir_intrinsic_memory_barrier:
   case nir_intrinsic_memory_barrier_image:
   case nir_intrinsic_memory_barrier_buffer:
   case nir_intrinsic_group_memory_barrier:
      return make_stores_ack_and_waitack();
   default:
      return false;
   }
}

bool EmitSSBOInstruction::emit_atomic(const nir_intrinsic_instr* instr)
{
   ESDOp op = get_opcode(instr->intrinsic);

   if (DS_OP_INVALID == op)
      return false;

   GPRVector dest = make_dest(instr);

   int base = remap_atomic_base(nir_intrinsic_base(instr));

   PValue uav_id = from_nir(instr->src[0], 0);

   PValue value = from_nir_with_fetch_constant(instr->src[1], 0);

   GDSInstr *ir = nullptr;
   if (instr->intrinsic == nir_intrinsic_atomic_counter_comp_swap)  {
      PValue value2 = from_nir_with_fetch_constant(instr->src[2], 0);
      ir = new GDSInstr(op, dest, value, value2, uav_id, base);
   } else {
      ir = new GDSInstr(op, dest, value, uav_id, base);
   }

   emit_instruction(ir);
   return true;
}

bool EmitSSBOInstruction::emit_unary_atomic(const nir_intrinsic_instr* instr)
{
   ESDOp op = get_opcode(instr->intrinsic);

   if (DS_OP_INVALID == op)
      return false;

   GPRVector dest = make_dest(instr);

   PValue uav_id = from_nir(instr->src[0], 0);

   auto ir = new GDSInstr(op, dest, uav_id, remap_atomic_base(nir_intrinsic_base(instr)));

   emit_instruction(ir);
   return true;
}

ESDOp EmitSSBOInstruction::get_opcode(const nir_intrinsic_op opcode)
{
   switch (opcode) {
   case nir_intrinsic_atomic_counter_add:
      return DS_OP_ADD_RET;
   case nir_intrinsic_atomic_counter_and:
      return DS_OP_AND_RET;
   case nir_intrinsic_atomic_counter_exchange:
      return DS_OP_XCHG_RET;
   case nir_intrinsic_atomic_counter_inc:
      return DS_OP_INC_RET;
   case nir_intrinsic_atomic_counter_max:
      return DS_OP_MAX_UINT_RET;
   case nir_intrinsic_atomic_counter_min:
      return DS_OP_MIN_UINT_RET;
   case nir_intrinsic_atomic_counter_or:
      return DS_OP_OR_RET;
   case nir_intrinsic_atomic_counter_read:
      return DS_OP_READ_RET;
   case nir_intrinsic_atomic_counter_xor:
      return DS_OP_XOR_RET;
   case nir_intrinsic_atomic_counter_post_dec:
      return DS_OP_DEC_RET;
   case nir_intrinsic_atomic_counter_comp_swap:
      return DS_OP_CMP_XCHG_RET;
   case nir_intrinsic_atomic_counter_pre_dec:
   default:
      return DS_OP_INVALID;
   }
}

RatInstruction::ERatOp
EmitSSBOInstruction::get_rat_opcode(const nir_intrinsic_op opcode, pipe_format format) const
{
   switch (opcode) {
   case nir_intrinsic_ssbo_atomic_add:
   case nir_intrinsic_image_atomic_add:
      return RatInstruction::ADD_RTN;
   case nir_intrinsic_ssbo_atomic_and:
   case nir_intrinsic_image_atomic_and:
      return RatInstruction::AND_RTN;
   case nir_intrinsic_ssbo_atomic_exchange:
   case nir_intrinsic_image_atomic_exchange:
      return RatInstruction::XCHG_RTN;
   case nir_intrinsic_ssbo_atomic_or:
   case nir_intrinsic_image_atomic_or:
      return RatInstruction::OR_RTN;
   case nir_intrinsic_ssbo_atomic_imin:
   case nir_intrinsic_image_atomic_imin:
      return RatInstruction::MIN_INT_RTN;
   case nir_intrinsic_ssbo_atomic_imax:
   case nir_intrinsic_image_atomic_imax:
      return RatInstruction::MAX_INT_RTN;
   case nir_intrinsic_ssbo_atomic_umin:
   case nir_intrinsic_image_atomic_umin:
      return RatInstruction::MIN_UINT_RTN;
   case nir_intrinsic_ssbo_atomic_umax:
   case nir_intrinsic_image_atomic_umax:
      return RatInstruction::MAX_UINT_RTN;
   case nir_intrinsic_ssbo_atomic_xor:
   case nir_intrinsic_image_atomic_xor:
      return RatInstruction::XOR_RTN;
   case nir_intrinsic_ssbo_atomic_comp_swap:
   case nir_intrinsic_image_atomic_comp_swap:
      if (util_format_is_float(format))
         return RatInstruction::CMPXCHG_FLT_RTN;
      else
         return RatInstruction::CMPXCHG_INT_RTN;
   case nir_intrinsic_image_load:
      return RatInstruction::NOP_RTN;
   default:
      unreachable("Unsupported RAT instruction");
   }
}


bool EmitSSBOInstruction::emit_atomic_add(const nir_intrinsic_instr* instr)
{
   GPRVector dest = make_dest(instr);

   PValue value = from_nir_with_fetch_constant(instr->src[1], 0);

   PValue uav_id = from_nir(instr->src[0], 0);

   auto ir = new GDSInstr(DS_OP_ADD_RET, dest, value, uav_id,
                          remap_atomic_base(nir_intrinsic_base(instr)));

   emit_instruction(ir);
   return true;
}

bool EmitSSBOInstruction::load_atomic_inc_limits()
{
   m_atomic_update = get_temp_register();
   m_atomic_update->set_keep_alive();
   emit_instruction(new AluInstruction(op1_mov, m_atomic_update, literal(1),
   {alu_write, alu_last_instr}));
   return true;
}

bool EmitSSBOInstruction::emit_atomic_inc(const nir_intrinsic_instr* instr)
{
   PValue uav_id = from_nir(instr->src[0], 0);
   GPRVector dest = make_dest(instr);
   auto ir = new GDSInstr(DS_OP_ADD_RET, dest, m_atomic_update, uav_id,
                          remap_atomic_base(nir_intrinsic_base(instr)));
   emit_instruction(ir);
   return true;
}

bool EmitSSBOInstruction::emit_atomic_pre_dec(const nir_intrinsic_instr *instr)
{
   GPRVector dest = make_dest(instr);

   PValue uav_id = from_nir(instr->src[0], 0);

   auto ir = new GDSInstr(DS_OP_SUB_RET, dest, m_atomic_update, uav_id,
                          remap_atomic_base(nir_intrinsic_base(instr)));
   emit_instruction(ir);

   emit_instruction(new AluInstruction(op2_sub_int,  dest.x(), dest.x(), literal(1), last_write));

   return true;
}

bool EmitSSBOInstruction::emit_load_ssbo(const nir_intrinsic_instr* instr)
{
   GPRVector dest = make_dest(instr);

   /** src0 not used, should be some offset */
   auto addr = from_nir(instr->src[1], 0);
   PValue addr_temp = create_register_from_nir_src(instr->src[1], 1);

   /** Should be lowered in nir */
   emit_instruction(new AluInstruction(op2_lshr_int, addr_temp, {addr, PValue(new LiteralValue(2))},
                    {alu_write, alu_last_instr}));

   const EVTXDataFormat formats[4] = {
      fmt_32,
      fmt_32_32,
      fmt_32_32_32,
      fmt_32_32_32_32
   };

   const std::array<int,4> dest_swt[4] = {
      {0,7,7,7},
      {0,1,7,7},
      {0,1,2,7},
      {0,1,2,3}
   };

   /* TODO fix resource index */
   auto ir = new FetchInstruction(dest, addr_temp,
                                  R600_IMAGE_REAL_RESOURCE_OFFSET + m_ssbo_image_offset
                                  , from_nir(instr->src[0], 0),
                                  formats[nir_dest_num_components(instr->dest) - 1], vtx_nf_int);
   ir->set_dest_swizzle(dest_swt[nir_dest_num_components(instr->dest) - 1]);
   ir->set_flag(vtx_use_tc);

   emit_instruction(ir);
   return true;
}

bool EmitSSBOInstruction::emit_store_ssbo(const nir_intrinsic_instr* instr)
{

   GPRVector::Swizzle swz = {7,7,7,7};
   for (unsigned i = 0; i <  nir_src_num_components(instr->src[0]); ++i)
      swz[i] = i;

   auto orig_addr = from_nir(instr->src[2], 0);

   int temp1 = allocate_temp_register();
   GPRVector addr_vec(temp1, {0,1,2,7});

   auto temp2 = get_temp_vec4();

   auto rat_id = from_nir(instr->src[1], 0);

   emit_instruction(new AluInstruction(op2_lshr_int, addr_vec.reg_i(0), orig_addr,
                                       PValue(new LiteralValue(2)), write));
   emit_instruction(new AluInstruction(op1_mov, addr_vec.reg_i(1), Value::zero, write));
   emit_instruction(new AluInstruction(op1_mov, addr_vec.reg_i(2), Value::zero, last_write));


//#define WRITE_AS_VECTOR
#ifdef WRITE_AS_VECTOR
   std::unique_ptr<GPRVector> value(vec_from_nir_with_fetch_constant(instr->src[0],
                                    (1 << instr->src[0].ssa->num_components) - 1, swz));

   /* TODO fix resource index */
   int nelements = instr->src[0].ssa->num_components - 1;
   if (nelements == 2)
      nelements = 3;
   auto ir = new RatInstruction(cf_mem_rat, RatInstruction::STORE_TYPED,
                                *value, addr_vec, 0, rat_id, 11,
                                (1 << instr->src[0].ssa->num_components) - 1,
                                0, false);
   emit_instruction(ir);
#else

   auto values = vec_from_nir_with_fetch_constant(instr->src[0],
         (1 << nir_src_num_components(instr->src[0])) - 1, {0,1,2,3}, true);

   auto cf_op = cf_mem_rat;
   //auto cf_op = nir_intrinsic_access(instr) & ACCESS_COHERENT ? cf_mem_rat_cacheless : cf_mem_rat;
   auto store = new RatInstruction(cf_op, RatInstruction::STORE_TYPED,
                                   values, addr_vec, m_ssbo_image_offset, rat_id, 1,
                                   1, 0, false);
   emit_instruction(store);
   m_store_ops.push_back(store);

   for (unsigned i = 1; i < nir_src_num_components(instr->src[0]); ++i) {
      emit_instruction(new AluInstruction(op1_mov, temp2.reg_i(0), from_nir(instr->src[0], i), write));
      emit_instruction(new AluInstruction(op2_add_int, addr_vec.reg_i(0),
                                          {addr_vec.reg_i(0), Value::one_i}, last_write));
      store = new RatInstruction(cf_op, RatInstruction::STORE_TYPED,
                                 temp2, addr_vec, 0, rat_id, 1,
                                 1, 0, false);
      emit_instruction(store);
      if (!(nir_intrinsic_access(instr) & ACCESS_COHERENT))
         m_store_ops.push_back(store);
   }
#endif
   return true;
}

bool
EmitSSBOInstruction::emit_image_store(const nir_intrinsic_instr *intrin)
{
   int imageid = 0;
   PValue image_offset;

   if (nir_src_is_const(intrin->src[0]))
      imageid = nir_src_as_int(intrin->src[0]);
   else
      image_offset = from_nir(intrin->src[0], 0);

   auto coord =  vec_from_nir_with_fetch_constant(intrin->src[1], 0xf, {0,1,2,3});
   auto undef = from_nir(intrin->src[2], 0);
   auto value = vec_from_nir_with_fetch_constant(intrin->src[3],  0xf, {0,1,2,3});
   auto unknown  = from_nir(intrin->src[4], 0);

   if (nir_intrinsic_image_dim(intrin) == GLSL_SAMPLER_DIM_1D &&
       nir_intrinsic_image_array(intrin)) {
      emit_instruction(new AluInstruction(op1_mov, coord.reg_i(2), coord.reg_i(1), {alu_write}));
      emit_instruction(new AluInstruction(op1_mov, coord.reg_i(1), coord.reg_i(2), {alu_last_instr, alu_write}));
   }

   auto op = cf_mem_rat; //nir_intrinsic_access(intrin) & ACCESS_COHERENT ? cf_mem_rat_cacheless : cf_mem_rat;
   auto store = new RatInstruction(op, RatInstruction::STORE_TYPED, value, coord, imageid,
                                   image_offset, 1, 0xf, 0, false);

   //if (!(nir_intrinsic_access(intrin) & ACCESS_COHERENT))
      m_store_ops.push_back(store);

   emit_instruction(store);
   return true;
}

bool
EmitSSBOInstruction::emit_ssbo_atomic_op(const nir_intrinsic_instr *intrin)
{
   int imageid = 0;
   PValue image_offset;

   if (nir_src_is_const(intrin->src[0]))
      imageid = nir_src_as_int(intrin->src[0]);
   else
      image_offset = from_nir(intrin->src[0], 0);

   auto opcode = EmitSSBOInstruction::get_rat_opcode(intrin->intrinsic, PIPE_FORMAT_R32_UINT);


   auto coord_orig =  from_nir(intrin->src[1], 0, 0);
   auto coord = get_temp_register(0);

   emit_instruction(new AluInstruction(op2_lshr_int, coord, coord_orig, literal(2), last_write));

   if (intrin->intrinsic == nir_intrinsic_ssbo_atomic_comp_swap) {
      emit_instruction(new AluInstruction(op1_mov, m_rat_return_address.reg_i(0),
                                          from_nir(intrin->src[3], 0), {alu_write}));
      // TODO: cayman wants channel 2 here
      emit_instruction(new AluInstruction(op1_mov, m_rat_return_address.reg_i(3),
                                          from_nir(intrin->src[2], 0), {alu_last_instr, alu_write}));
   } else {
      emit_instruction(new AluInstruction(op1_mov, m_rat_return_address.reg_i(0),
                                          from_nir(intrin->src[2], 0), {alu_write}));
      emit_instruction(new AluInstruction(op1_mov, m_rat_return_address.reg_i(2), Value::zero, last_write));
   }


   GPRVector out_vec({coord, coord, coord, coord});

   auto atomic = new RatInstruction(cf_mem_rat, opcode, m_rat_return_address, out_vec, imageid + m_ssbo_image_offset,
                                   image_offset, 1, 0xf, 0, true);
   emit_instruction(atomic);
   emit_instruction(new WaitAck(0));

   GPRVector dest = vec_from_nir(intrin->dest, intrin->dest.ssa.num_components);
   auto fetch = new FetchInstruction(vc_fetch,
                                     no_index_offset,
                                     fmt_32,
                                     vtx_nf_int,
                                     vtx_es_none,
                                     m_rat_return_address.reg_i(1),
                                     dest,
                                     0,
                                     false,
                                     0xf,
                                     R600_IMAGE_IMMED_RESOURCE_OFFSET + imageid,
                                     0,
                                     bim_none,
                                     false,
                                     false,
                                     0,
                                     0,
                                     0,
                                     image_offset,
                                     {0,7,7,7});
   fetch->set_flag(vtx_srf_mode);
   fetch->set_flag(vtx_use_tc);
   emit_instruction(fetch);
   return true;

}

bool
EmitSSBOInstruction::emit_image_load(const nir_intrinsic_instr *intrin)
{
   int imageid = 0;
   PValue image_offset;

   if (nir_src_is_const(intrin->src[0]))
      imageid = nir_src_as_int(intrin->src[0]);
   else
      image_offset = from_nir(intrin->src[0], 0);

   auto rat_op = get_rat_opcode(intrin->intrinsic, nir_intrinsic_format(intrin));

   GPRVector::Swizzle swz = {0,1,2,3};
   auto coord =  vec_from_nir_with_fetch_constant(intrin->src[1], 0xf, swz);

   if (nir_intrinsic_image_dim(intrin) == GLSL_SAMPLER_DIM_1D &&
       nir_intrinsic_image_array(intrin)) {
      emit_instruction(new AluInstruction(op1_mov, coord.reg_i(2), coord.reg_i(1), {alu_write}));
      emit_instruction(new AluInstruction(op1_mov, coord.reg_i(1), coord.reg_i(2), {alu_last_instr, alu_write}));
   }

   if (intrin->intrinsic != nir_intrinsic_image_load) {
      if (intrin->intrinsic == nir_intrinsic_image_atomic_comp_swap) {
         emit_instruction(new AluInstruction(op1_mov, m_rat_return_address.reg_i(0),
                                             from_nir(intrin->src[4], 0), {alu_write}));
         emit_instruction(new AluInstruction(op1_mov, m_rat_return_address.reg_i(3),
                                             from_nir(intrin->src[3], 0), {alu_last_instr, alu_write}));
      } else {
         emit_instruction(new AluInstruction(op1_mov, m_rat_return_address.reg_i(0),
                                             from_nir(intrin->src[3], 0), {alu_last_instr, alu_write}));
      }
   }
   auto cf_op = cf_mem_rat;// nir_intrinsic_access(intrin) & ACCESS_COHERENT ? cf_mem_rat_cacheless : cf_mem_rat;

   auto store = new RatInstruction(cf_op, rat_op, m_rat_return_address, coord, imageid,
                                   image_offset, 1, 0xf, 0, true);
   emit_instruction(store);
   return fetch_return_value(intrin);
}

bool EmitSSBOInstruction::fetch_return_value(const nir_intrinsic_instr *intrin)
{
   emit_instruction(new WaitAck(0));

   pipe_format format = nir_intrinsic_format(intrin);
   unsigned fmt = fmt_32;
   unsigned num_format = 0;
   unsigned format_comp = 0;
   unsigned endian = 0;

   int imageid = 0;
   PValue image_offset;

   if (nir_src_is_const(intrin->src[0]))
      imageid = nir_src_as_int(intrin->src[0]);
   else
      image_offset = from_nir(intrin->src[0], 0);

   r600_vertex_data_type(format, &fmt, &num_format, &format_comp, &endian);

   GPRVector dest = vec_from_nir(intrin->dest, nir_dest_num_components(intrin->dest));

   auto fetch = new FetchInstruction(vc_fetch,
                                     no_index_offset,
                                     (EVTXDataFormat)fmt,
                                     (EVFetchNumFormat)num_format,
                                     (EVFetchEndianSwap)endian,
                                     m_rat_return_address.reg_i(1),
                                     dest,
                                     0,
                                     false,
                                     0x3,
                                     R600_IMAGE_IMMED_RESOURCE_OFFSET + imageid,
                                     0,
                                     bim_none,
                                     false,
                                     false,
                                     0,
                                     0,
                                     0,
                                     image_offset, {0,1,2,3});
   fetch->set_flag(vtx_srf_mode);
   fetch->set_flag(vtx_use_tc);
   if (format_comp)
      fetch->set_flag(vtx_format_comp_signed);

   emit_instruction(fetch);
   return true;
}

bool EmitSSBOInstruction::emit_image_size(const nir_intrinsic_instr *intrin)
{
   GPRVector dest = vec_from_nir(intrin->dest, nir_dest_num_components(intrin->dest));
   GPRVector src{0,{4,4,4,4}};

   assert(nir_src_as_uint(intrin->src[1]) == 0);

   auto const_offset = nir_src_as_const_value(intrin->src[0]);
   auto dyn_offset = PValue();
   int res_id = R600_IMAGE_REAL_RESOURCE_OFFSET;
   if (const_offset)
      res_id += const_offset[0].u32;
   else
      dyn_offset = from_nir(intrin->src[0], 0);

   if (nir_intrinsic_image_dim(intrin) == GLSL_SAMPLER_DIM_BUF) {
      emit_instruction(new FetchInstruction(dest, PValue(new GPRValue(0, 7)),
                       res_id,
                       bim_none));
      return true;
   } else {
      emit_instruction(new TexInstruction(TexInstruction::get_resinfo, dest, src,
                                             0/* ?? */,
                                             res_id, dyn_offset));
      if (nir_intrinsic_image_dim(intrin) == GLSL_SAMPLER_DIM_CUBE &&
          nir_intrinsic_image_array(intrin) && nir_dest_num_components(intrin->dest) > 2) {
         /* Need to load the layers from a const buffer */

         unsigned lookup_resid = const_offset[0].u32;
         emit_instruction(new AluInstruction(op1_mov, dest.reg_i(2),
                                             PValue(new UniformValue(lookup_resid/4 + R600_SHADER_BUFFER_INFO_SEL, lookup_resid % 4,
                                                                     R600_BUFFER_INFO_CONST_BUFFER)),
         EmitInstruction::last_write));
      }
   }
   return true;
}

bool EmitSSBOInstruction::emit_buffer_size(const nir_intrinsic_instr *intr)
{
   std::array<PValue,4> dst_elms;


   for (uint16_t i = 0; i < 4; ++i) {
      dst_elms[i] = from_nir(intr->dest, (i < intr->dest.ssa.num_components) ? i : 7);
   }

   GPRVector dst(dst_elms);
   GPRVector src(0,{4,4,4,4});

   auto const_offset = nir_src_as_const_value(intr->src[0]);
   auto dyn_offset = PValue();
   int res_id = R600_IMAGE_REAL_RESOURCE_OFFSET;
   if (const_offset)
      res_id += const_offset[0].u32;
   else
      assert(0 && "dynamic buffer offset not supported in buffer_size");

   emit_instruction(new FetchInstruction(dst, PValue(new GPRValue(0, 7)),
                    res_id, bim_none));

   return true;
}

bool EmitSSBOInstruction::make_stores_ack_and_waitack()
{
   for (auto&& store: m_store_ops)
      store->set_ack();

   if (!m_store_ops.empty())
      emit_instruction(new WaitAck(0));

   m_store_ops.clear();

   return true;
}

GPRVector EmitSSBOInstruction::make_dest(const nir_intrinsic_instr* ir)
{
   GPRVector::Values v;
   int i;
   for (i = 0; i < 4; ++i)
      v[i] = from_nir(ir->dest, i);
   return GPRVector(v);
}

}
