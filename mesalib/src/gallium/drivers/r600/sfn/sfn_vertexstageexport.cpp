#include "sfn_vertexstageexport.h"

#include "sfn_shaderio.h"

namespace r600 {

using std::priority_queue;

VertexStageExportBase::VertexStageExportBase(VertexStage& proc):
   m_proc(proc),
   m_cur_clip_pos(1)
{

}

VertexStageExportBase::~VertexStageExportBase()
{

}

bool VertexStageExportBase::do_process_outputs(nir_variable *output)
{
   return true;
}

void VertexStageExportBase::emit_shader_start()
{

}

void VertexStageExportBase::scan_store_output(nir_intrinsic_instr* instr)
{

}

bool VertexStageExportBase::store_output(nir_intrinsic_instr* instr)
{
   auto index = nir_src_as_const_value(instr->src[1]);
   assert(index && "Indirect outputs not supported");

   const store_loc store_info  = {
      nir_intrinsic_component(instr),
      nir_intrinsic_io_semantics(instr).location,
      (unsigned)nir_intrinsic_base(instr) + index->u32,
      0
   };

   return do_store_output(store_info, instr);
}

VertexStageExportForFS::VertexStageExportForFS(VertexStage& proc,
                                               const pipe_stream_output_info *so_info,
                                               r600_pipe_shader *pipe_shader, const r600_shader_key &key):
   VertexStageWithOutputInfo(proc),
   m_last_param_export(nullptr),
   m_last_pos_export(nullptr),
   m_num_clip_dist(0),
   m_enabled_stream_buffers_mask(0),
   m_so_info(so_info),
   m_pipe_shader(pipe_shader),
   m_key(key)
{
}

bool VertexStageWithOutputInfo::do_process_outputs(nir_variable *output)
{
   if (output->data.location == VARYING_SLOT_COL0 ||
       output->data.location == VARYING_SLOT_COL1 ||
       (output->data.location >= VARYING_SLOT_VAR0 &&
       output->data.location <= VARYING_SLOT_VAR31) ||
       (output->data.location >= VARYING_SLOT_TEX0 &&
        output->data.location <= VARYING_SLOT_TEX7) ||
       output->data.location == VARYING_SLOT_BFC0 ||
       output->data.location == VARYING_SLOT_BFC1 ||
       output->data.location == VARYING_SLOT_CLIP_VERTEX ||
       output->data.location == VARYING_SLOT_CLIP_DIST0 ||
       output->data.location == VARYING_SLOT_CLIP_DIST1 ||
       output->data.location == VARYING_SLOT_POS ||
       output->data.location == VARYING_SLOT_PSIZ ||
       output->data.location == VARYING_SLOT_FOGC ||
       output->data.location == VARYING_SLOT_LAYER ||
       output->data.location == VARYING_SLOT_EDGE ||
       output->data.location == VARYING_SLOT_VIEWPORT
       ) {

      r600_shader_io& io = m_proc.sh_info().output[output->data.driver_location];
      auto semantic = r600_get_varying_semantic(output->data.location);
      io.name = semantic.first;
      io.sid = semantic.second;

      m_proc.evaluate_spi_sid(io);
      io.write_mask = ((1 << glsl_get_components(output->type)) - 1)
                      << output->data.location_frac;
      ++m_proc.sh_info().noutput;

      if (output->data.location == VARYING_SLOT_PSIZ ||
          output->data.location == VARYING_SLOT_EDGE ||
          output->data.location == VARYING_SLOT_LAYER) // VIEWPORT?
            m_cur_clip_pos = 2;

      if (output->data.location != VARYING_SLOT_POS &&
          output->data.location != VARYING_SLOT_EDGE &&
          output->data.location != VARYING_SLOT_PSIZ &&
          output->data.location != VARYING_SLOT_CLIP_VERTEX)
         m_param_driver_locations.push(output->data.driver_location);

      return true;
   }
   return false;
}

bool VertexStageExportForFS::do_store_output(const store_loc& store_info, nir_intrinsic_instr* instr)
{
   switch (store_info.location) {
   case VARYING_SLOT_PSIZ:
      m_proc.sh_info().vs_out_point_size = 1;
      m_proc.sh_info().vs_out_misc_write = 1;
      FALLTHROUGH;
   case VARYING_SLOT_POS:
      return emit_varying_pos(store_info, instr);
   case VARYING_SLOT_EDGE: {
      std::array<uint32_t, 4> swizzle_override = {7 ,0, 7, 7};
      return emit_varying_pos(store_info, instr, &swizzle_override);
   }
   case VARYING_SLOT_VIEWPORT: {
      std::array<uint32_t, 4> swizzle_override = {7, 7, 7, 0};
      return emit_varying_pos(store_info, instr, &swizzle_override) &&
            emit_varying_param(store_info, instr);
   }
   case VARYING_SLOT_CLIP_VERTEX:
      return emit_clip_vertices(store_info, instr);
   case VARYING_SLOT_CLIP_DIST0:
   case VARYING_SLOT_CLIP_DIST1:
      m_num_clip_dist += 4;
      return emit_varying_param(store_info, instr) && emit_varying_pos(store_info, instr);
   case VARYING_SLOT_LAYER: {
      m_proc.sh_info().vs_out_misc_write = 1;
      m_proc.sh_info().vs_out_layer = 1;
      std::array<uint32_t, 4> swz = {7,7,0,7};
      return emit_varying_pos(store_info, instr, &swz) &&
            emit_varying_param(store_info, instr);
   }
   case VARYING_SLOT_VIEW_INDEX:
      return emit_varying_pos(store_info, instr) &&
            emit_varying_param(store_info, instr);

   default:
         return emit_varying_param(store_info, instr);
   }

   fprintf(stderr, "r600-NIR: Unimplemented store_deref for %d\n",
           store_info.location);
   return false;
}

bool VertexStageExportForFS::emit_varying_pos(const store_loc &store_info, nir_intrinsic_instr* instr,
                                              std::array<uint32_t, 4> *swizzle_override)
{
   std::array<uint32_t,4> swizzle;
   uint32_t write_mask = 0;

   if (swizzle_override) {
      swizzle = *swizzle_override;
      for (int i = 0; i < 4; ++i) {
         if (swizzle[i] < 6)
            write_mask |= 1 << i;
      }
   } else {
      write_mask = nir_intrinsic_write_mask(instr) << store_info.frac;
      for (int i = 0; i < 4; ++i)
         swizzle[i] = ((1 << i) & write_mask) ? i - store_info.frac : 7;
   }

   m_proc.sh_info().output[store_info.driver_location].write_mask = write_mask;

   GPRVector value = m_proc.vec_from_nir_with_fetch_constant(instr->src[store_info.data_loc], write_mask, swizzle);
   m_proc.set_output(store_info.driver_location, value.sel());

   int export_slot = 0;

   switch (store_info.location) {
   case VARYING_SLOT_EDGE: {
      m_proc.sh_info().vs_out_misc_write = 1;
      m_proc.sh_info().vs_out_edgeflag = 1;
      m_proc.emit_instruction(op1_mov, value.reg_i(1), {value.reg_i(1)}, {alu_write, alu_dst_clamp, alu_last_instr});
      m_proc.emit_instruction(op1_flt_to_int, value.reg_i(1), {value.reg_i(1)}, {alu_write, alu_last_instr});
      m_proc.sh_info().output[store_info.driver_location].write_mask = 0xf;
   }
      FALLTHROUGH;
   case VARYING_SLOT_PSIZ:
   case VARYING_SLOT_LAYER:
      export_slot = 1;
      break;
   case VARYING_SLOT_VIEWPORT:
      m_proc.sh_info().vs_out_misc_write = 1;
      m_proc.sh_info().vs_out_viewport = 1;
      export_slot = 1;
      break;
   case VARYING_SLOT_POS:
      break;
   case VARYING_SLOT_CLIP_DIST0:
   case VARYING_SLOT_CLIP_DIST1:
      export_slot = m_cur_clip_pos++;
      break;
   default:
      sfn_log << SfnLog::err << __func__ << "Unsupported location "
              << store_info.location << "\n";
      return false;
   }

   m_last_pos_export = new ExportInstruction(export_slot, value, ExportInstruction::et_pos);
   m_proc.emit_export_instruction(m_last_pos_export);
   m_proc.add_param_output_reg(store_info.driver_location, m_last_pos_export->gpr_ptr());
   return true;
}

bool VertexStageExportForFS::emit_varying_param(const store_loc &store_info, nir_intrinsic_instr* instr)
{
   assert(store_info.driver_location < m_proc.sh_info().noutput);
   sfn_log << SfnLog::io << __func__ << ": emit DDL: " << store_info.driver_location << "\n";

   int write_mask = nir_intrinsic_write_mask(instr) << store_info.frac;
   std::array<uint32_t,4> swizzle;
   for (int i = 0; i < 4; ++i)
      swizzle[i] = ((1 << i) & write_mask) ? i - store_info.frac : 7;

   //m_proc.sh_info().output[store_info.driver_location].write_mask = write_mask;

   GPRVector value = m_proc.vec_from_nir_with_fetch_constant(instr->src[store_info.data_loc], write_mask, swizzle, true);
   m_proc.sh_info().output[store_info.driver_location].gpr = value.sel();

   /* This should use the registers!! */
   m_proc.set_output(store_info.driver_location, value.sel());

   m_last_param_export = new ExportInstruction(param_id(store_info.driver_location),
                                               value, ExportInstruction::et_param);
   m_proc.emit_export_instruction(m_last_param_export);
   m_proc.add_param_output_reg(store_info.driver_location, m_last_param_export->gpr_ptr());
   return true;
}

bool VertexStageExportForFS::emit_clip_vertices(const store_loc &store_info, nir_intrinsic_instr* instr)
{
   m_proc.sh_info().cc_dist_mask = 0xff;
   m_proc.sh_info().clip_dist_write = 0xff;

   m_clip_vertex = m_proc.vec_from_nir_with_fetch_constant(instr->src[store_info.data_loc], 0xf, {0,1,2,3});
   m_proc.add_param_output_reg(store_info.driver_location, &m_clip_vertex);

   for (int i = 0; i < 4; ++i)
      m_proc.sh_info().output[store_info.driver_location].write_mask |= 1 << i;

   GPRVector clip_dist[2] = { m_proc.get_temp_vec4(), m_proc.get_temp_vec4()};

   for (int i = 0; i < 8; i++) {
      int oreg = i >> 2;
      int ochan = i & 3;
      AluInstruction *ir = nullptr;
      for (int j = 0; j < 4; j++) {
         ir = new AluInstruction(op2_dot4_ieee, clip_dist[oreg].reg_i(j), m_clip_vertex.reg_i(j),
                                 PValue(new UniformValue(512 + i, j, R600_BUFFER_INFO_CONST_BUFFER)),
                                 (j == ochan) ? EmitInstruction::write : EmitInstruction::empty);
         m_proc.emit_instruction(ir);
      }
      ir->set_flag(alu_last_instr);
   }

   m_last_pos_export = new ExportInstruction(m_cur_clip_pos++, clip_dist[0], ExportInstruction::et_pos);
   m_proc.emit_export_instruction(m_last_pos_export);

   m_last_pos_export = new ExportInstruction(m_cur_clip_pos, clip_dist[1], ExportInstruction::et_pos);
   m_proc.emit_export_instruction(m_last_pos_export);

   return true;
}

VertexStageWithOutputInfo::VertexStageWithOutputInfo(VertexStage& proc):
   VertexStageExportBase(proc),
   m_current_param(0)
{

}

void VertexStageWithOutputInfo::scan_store_output(nir_intrinsic_instr* instr)
{
   auto location = nir_intrinsic_io_semantics(instr).location;
   auto driver_location = nir_intrinsic_base(instr);
   auto index = nir_src_as_const_value(instr->src[1]);
   assert(index);

   unsigned noutputs = driver_location + index->u32 + 1;
   if (m_proc.sh_info().noutput < noutputs)
      m_proc.sh_info().noutput = noutputs;

   r600_shader_io& io = m_proc.sh_info().output[driver_location + index->u32];
   auto semantic = r600_get_varying_semantic(location + index->u32);
   io.name = semantic.first;
   io.sid = semantic.second;
   m_proc.evaluate_spi_sid(io);
   io.write_mask = nir_intrinsic_write_mask(instr);

   if (location == VARYING_SLOT_PSIZ ||
       location == VARYING_SLOT_EDGE ||
       location == VARYING_SLOT_LAYER) // VIEWPORT?
      m_cur_clip_pos = 2;

   if (location != VARYING_SLOT_POS &&
       location != VARYING_SLOT_EDGE &&
       location != VARYING_SLOT_PSIZ &&
       location != VARYING_SLOT_CLIP_VERTEX) {
      m_param_driver_locations.push(driver_location + index->u32);
   }
}

unsigned VertexStageWithOutputInfo::param_id(unsigned driver_location)
{
   auto param_loc = m_param_map.find(driver_location);
   assert(param_loc != m_param_map.end());
   return param_loc->second;
}

void VertexStageWithOutputInfo::emit_shader_start()
{
   while (!m_param_driver_locations.empty()) {
      auto loc = m_param_driver_locations.top();
      m_param_driver_locations.pop();
      m_param_map[loc] = m_current_param++;
   }
}

unsigned VertexStageWithOutputInfo::current_param() const
{
   return m_current_param;
}

void VertexStageExportForFS::finalize_exports()
{
   if (m_key.vs.as_gs_a) {
      PValue o(new GPRValue(0,PIPE_SWIZZLE_0));
      GPRVector primid({m_proc.primitive_id(), o,o,o});
      m_last_param_export = new ExportInstruction(current_param(), primid, ExportInstruction::et_param);
      m_proc.emit_export_instruction(m_last_param_export);
      int i;
      i = m_proc.sh_info().noutput++;
      auto& io = m_proc.sh_info().output[i];
      io.name = TGSI_SEMANTIC_PRIMID;
      io.sid = 0;
      io.gpr = 0;
      io.interpolate = TGSI_INTERPOLATE_CONSTANT;
      io.write_mask = 0x1;
      io.spi_sid = m_key.vs.prim_id_out;
      m_proc.sh_info().vs_as_gs_a = 1;
   }

   if (m_so_info && m_so_info->num_outputs)
      emit_stream(-1);

   m_pipe_shader->enabled_stream_buffers_mask = m_enabled_stream_buffers_mask;

   if (!m_last_param_export) {
      GPRVector value(0,{7,7,7,7});
      m_last_param_export = new ExportInstruction(0, value, ExportInstruction::et_param);
      m_proc.emit_export_instruction(m_last_param_export);
   }
   m_last_param_export->set_last();

   if (!m_last_pos_export) {
      GPRVector value(0,{7,7,7,7});
      m_last_pos_export = new ExportInstruction(0, value, ExportInstruction::et_pos);
      m_proc.emit_export_instruction(m_last_pos_export);
   }
   m_last_pos_export->set_last();
}

bool VertexStageExportForFS::emit_stream(int stream)
{
   assert(m_so_info);
   if (m_so_info->num_outputs > PIPE_MAX_SO_OUTPUTS) {
           R600_ERR("Too many stream outputs: %d\n", m_so_info->num_outputs);
           return false;
   }
   for (unsigned i = 0; i < m_so_info->num_outputs; i++) {
           if (m_so_info->output[i].output_buffer >= 4) {
                   R600_ERR("Exceeded the max number of stream output buffers, got: %d\n",
                            m_so_info->output[i].output_buffer);
                   return false;
           }
   }
   const GPRVector *so_gpr[PIPE_MAX_SHADER_OUTPUTS];
   unsigned start_comp[PIPE_MAX_SHADER_OUTPUTS];
   std::vector<GPRVector> tmp(m_so_info->num_outputs);

   /* Initialize locations where the outputs are stored. */
   for (unsigned i = 0; i < m_so_info->num_outputs; i++) {
      if (stream != -1 && stream != m_so_info->output[i].stream)
         continue;

      sfn_log << SfnLog::instr << "Emit stream " << i
              << " with register index " << m_so_info->output[i].register_index << "  so_gpr:";


      so_gpr[i] = m_proc.output_register(m_so_info->output[i].register_index);

      if (!so_gpr[i]) {
         sfn_log << SfnLog::err << "\nERR: register index "
                 << m_so_info->output[i].register_index
                 << " doesn't correspond to an output register\n";
         return false;
      }
      start_comp[i] = m_so_info->output[i].start_component;
      /* Lower outputs with dst_offset < start_component.
       *
       * We can only output 4D vectors with a write mask, e.g. we can
       * only output the W component at offset 3, etc. If we want
       * to store Y, Z, or W at buffer offset 0, we need to use MOV
       * to move it to X and output X. */
      if (m_so_info->output[i].dst_offset < m_so_info->output[i].start_component) {

         GPRVector::Swizzle swizzle =  {0,1,2,3};
         for (auto j = m_so_info->output[i].num_components; j < 4; ++j)
            swizzle[j] = 7;
         tmp[i] = m_proc.get_temp_vec4(swizzle);

         int sc = m_so_info->output[i].start_component;
         AluInstruction *alu = nullptr;
         for (int j = 0; j < m_so_info->output[i].num_components; j++) {
            alu = new AluInstruction(op1_mov, tmp[i][j], so_gpr[i]->reg_i(j + sc), {alu_write});
            m_proc.emit_instruction(alu);
         }
         if (alu)
            alu->set_flag(alu_last_instr);

         start_comp[i] = 0;
         so_gpr[i] = &tmp[i];
      }
      sfn_log << SfnLog::instr <<  *so_gpr[i] << "\n";
   }

   /* Write outputs to buffers. */
   for (unsigned i = 0; i < m_so_info->num_outputs; i++) {
      sfn_log << SfnLog::instr << "Write output buffer " << i
              << " with register index " << m_so_info->output[i].register_index << "\n";

      StreamOutIntruction *out_stream =
            new StreamOutIntruction(*so_gpr[i],
                                    m_so_info->output[i].num_components,
                                    m_so_info->output[i].dst_offset - start_comp[i],
                                    ((1 << m_so_info->output[i].num_components) - 1) << start_comp[i],
                                    m_so_info->output[i].output_buffer,
                                    m_so_info->output[i].stream);
      m_proc.emit_export_instruction(out_stream);
      m_enabled_stream_buffers_mask |= (1 << m_so_info->output[i].output_buffer) << m_so_info->output[i].stream * 4;
   }
   return true;
}


VertexStageExportForGS::VertexStageExportForGS(VertexStage &proc,
                                               const r600_shader *gs_shader):
   VertexStageWithOutputInfo(proc),
   m_num_clip_dist(0),
   m_gs_shader(gs_shader)
{

}

bool VertexStageExportForGS::do_store_output(const store_loc& store_info, nir_intrinsic_instr* instr)
{
   int ring_offset = -1;
   const r600_shader_io& out_io = m_proc.sh_info().output[store_info.driver_location];

   sfn_log << SfnLog::io << "check output " << store_info.driver_location
           << " name=" << out_io.name<< " sid=" << out_io.sid << "\n";
   for (unsigned k = 0; k < m_gs_shader->ninput; ++k) {
      auto& in_io = m_gs_shader->input[k];
      sfn_log << SfnLog::io << "  against  " <<  k << " name=" << in_io.name<< " sid=" << in_io.sid << "\n";

      if (in_io.name == out_io.name &&
          in_io.sid == out_io.sid) {
         ring_offset = in_io.ring_offset;
         break;
      }
   }

   if (store_info.location == VARYING_SLOT_VIEWPORT) {
      m_proc.sh_info().vs_out_viewport = 1;
      m_proc.sh_info().vs_out_misc_write = 1;
      return true;
   }

   if (ring_offset == -1) {
      sfn_log << SfnLog::err << "VS defines output at "
              << store_info.driver_location << "name=" << out_io.name
              << " sid=" << out_io.sid << " that is not consumed as GS input\n";
      return true;
   }

   uint32_t write_mask =  (1 << instr->num_components) - 1;

   GPRVector value = m_proc.vec_from_nir_with_fetch_constant(instr->src[store_info.data_loc], write_mask,
         swizzle_from_comps(instr->num_components), true);

   auto ir = new MemRingOutIntruction(cf_mem_ring, mem_write, value,
                                      ring_offset >> 2, 4, PValue());
   m_proc.emit_export_instruction(ir);

   m_proc.sh_info().output[store_info.driver_location].write_mask |= write_mask;
   if (store_info.location == VARYING_SLOT_CLIP_DIST0 ||
       store_info.location == VARYING_SLOT_CLIP_DIST1)
      m_num_clip_dist += 4;

   return true;
}

void VertexStageExportForGS::finalize_exports()
{

}

VertexStageExportForES::VertexStageExportForES(VertexStage& proc):
   VertexStageExportBase(proc)
{
}

bool VertexStageExportForES::do_store_output(const store_loc& store_info, nir_intrinsic_instr* instr)
{
   return true;
}

void VertexStageExportForES::finalize_exports()
{

}

}
