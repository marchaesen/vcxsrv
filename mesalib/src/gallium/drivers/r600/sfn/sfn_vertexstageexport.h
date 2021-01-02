#ifndef VERTEXSTAGEEXPORT_H
#define VERTEXSTAGEEXPORT_H

#include "sfn_shader_base.h"
#include <queue>

namespace r600 {

class VertexStage : public ShaderFromNirProcessor {
public:
   using ShaderFromNirProcessor::ShaderFromNirProcessor;

   virtual PValue primitive_id() = 0;
};

class VertexStageExportBase
{
public:
   VertexStageExportBase(VertexStage& proc);
   virtual ~VertexStageExportBase();
   virtual void finalize_exports() = 0;
   virtual bool do_process_outputs(nir_variable *output);

   virtual void emit_shader_start();

   virtual void scan_store_output(nir_intrinsic_instr* instr);
   bool store_output(nir_intrinsic_instr* instr);
protected:

   struct store_loc {
      unsigned frac;
      unsigned location;
      unsigned driver_location;
      int data_loc;
   };
   virtual bool do_store_output(const store_loc& store_info, nir_intrinsic_instr* instr) = 0;

   VertexStage& m_proc;
   int m_cur_clip_pos;
   GPRVector m_clip_vertex;
};


class VertexStageWithOutputInfo: public VertexStageExportBase
{
protected:
   VertexStageWithOutputInfo(VertexStage& proc);
   void scan_store_output(nir_intrinsic_instr* instr) override;
   void emit_shader_start() override;
   bool do_process_outputs(nir_variable *output) override;
protected:
   unsigned param_id(unsigned driver_location);
   unsigned current_param() const;
private:
   std::priority_queue<unsigned, std::vector<unsigned>, std::greater<unsigned> > m_param_driver_locations;
   std::map<unsigned, unsigned> m_param_map;
   unsigned m_current_param;
};


class VertexStageExportForFS : public VertexStageWithOutputInfo
{
public:
   VertexStageExportForFS(VertexStage& proc,
                          const pipe_stream_output_info *so_info,
                          r600_pipe_shader *pipe_shader,
                          const r600_shader_key& key);

   void finalize_exports() override;
private:
   bool do_store_output(const store_loc& store_info, nir_intrinsic_instr* instr) override;

   bool emit_varying_param(const store_loc& store_info, nir_intrinsic_instr* instr);
   bool emit_varying_pos(const store_loc& store_info, nir_intrinsic_instr* instr,
                         std::array<uint32_t, 4> *swizzle_override = nullptr);
   bool emit_clip_vertices(const store_loc &store_info, nir_intrinsic_instr* instr);
   bool emit_stream(int stream);

   ExportInstruction *m_last_param_export;
   ExportInstruction *m_last_pos_export;

   int m_num_clip_dist;
   int m_enabled_stream_buffers_mask;
   const pipe_stream_output_info *m_so_info;
   r600_pipe_shader *m_pipe_shader;
   const r600_shader_key& m_key;


};

class VertexStageExportForGS : public VertexStageWithOutputInfo
{
public:
   VertexStageExportForGS(VertexStage& proc,
                          const r600_shader *gs_shader);
   void finalize_exports() override;

private:
   bool do_store_output(const store_loc& store_info, nir_intrinsic_instr* instr) override;
   unsigned m_num_clip_dist;
   const r600_shader *m_gs_shader;
};

class VertexStageExportForES : public VertexStageExportBase
{
public:
   VertexStageExportForES(VertexStage& proc);
   void finalize_exports() override;
private:
   bool do_store_output(const store_loc& store_info, nir_intrinsic_instr* instr) override;
};


}

#endif // VERTEXSTAGEEXPORT_H
