#ifndef TEVALSHADERFROMNIR_H
#define TEVALSHADERFROMNIR_H

#include "sfn_shader_base.h"
#include "sfn_vertexstageexport.h"

namespace r600 {

class TEvalShaderFromNir : public VertexStage
{
public:
	TEvalShaderFromNir(r600_pipe_shader *sh, r600_pipe_shader_selector& sel,
                           const r600_shader_key& key, r600_shader *gs_shader,
                           enum chip_class chip_class);
        bool scan_sysvalue_access(nir_instr *instr) override;
        PValue primitive_id() override {return m_primitive_id;}
     private:
        void emit_shader_start() override;
        bool do_allocate_reserved_registers() override;
        bool emit_intrinsic_instruction_override(nir_intrinsic_instr* instr) override;
        bool emit_load_tess_coord(nir_intrinsic_instr* instr);
        bool load_tess_z_coord(nir_intrinsic_instr* instr);

        void do_finalize() override;


        unsigned m_reserved_registers;
        PValue m_tess_coord[3];
        PValue m_rel_patch_id;
        PValue m_primitive_id;

        std::unique_ptr<VertexStageExportBase> m_export_processor;
        const r600_shader_key& m_key;
};


}

#endif // TEVALSHADERFROMNIR_H
