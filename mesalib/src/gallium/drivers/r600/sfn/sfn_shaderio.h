/* -*- mesa-c++  -*-
 *
 * Copyright (c) 2018 Collabora LTD
 *
 * Author: Gert Wollny <gert.wollny@collabora.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * on the rights to use, copy, modify, merge, publish, distribute, sub
 * license, and/or sell copies of the Software, and to permit persons to whom
 * the Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHOR(S) AND/OR THEIR SUPPLIERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 * USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#ifndef SFN_SHADERIO_H
#define SFN_SHADERIO_H

#include "compiler/nir/nir.h"
#include "pipe/p_defines.h"
#include "pipe/p_shader_tokens.h"
#include "gallium/drivers/r600/r600_shader.h"

#include <vector>
#include <memory>

namespace r600 {

class ShaderInput {
public:
   ShaderInput();
   virtual  ~ShaderInput();

   ShaderInput(tgsi_semantic name);
   tgsi_semantic name() const {return m_name;}

   void set_gpr(int gpr) {m_gpr = gpr;}
   int gpr() const {return m_gpr;}
   void set_ioinfo(r600_shader_io& io, int translated_ij_index) const;

   virtual void set_lds_pos(int lds_pos);
   virtual int ij_index() const;
   virtual bool interpolate() const;
   virtual int lds_pos() const;
   void set_uses_interpolate_at_centroid();

   virtual bool is_varying() const;

private:
   virtual void set_specific_ioinfo(r600_shader_io& io) const;

   tgsi_semantic m_name;
   int m_gpr;
   bool m_uses_interpolate_at_centroid;
};

using PShaderInput = std::shared_ptr<ShaderInput>;

class ShaderInputSystemValue: public ShaderInput {
public:
   ShaderInputSystemValue(tgsi_semantic name, int gpr);
   void set_specific_ioinfo(r600_shader_io& io) const;
   int m_gpr;
};

class ShaderInputVarying : public ShaderInput {
public:
   ShaderInputVarying(tgsi_semantic _name, int sid, unsigned driver_location,
                      unsigned frac, unsigned components, tgsi_interpolate_mode interpolate,
                      tgsi_interpolate_loc interp_loc);
   ShaderInputVarying(tgsi_semantic name, int sid, nir_variable *input);
   ShaderInputVarying(tgsi_semantic name, const ShaderInputVarying& orig,
                      size_t location);

   void set_lds_pos(int lds_pos) override;

   int ij_index() const override;

   bool interpolate() const override;

   int lds_pos() const override;

   int sid() const {return m_sid;}

   void update_mask(int additional_comps, int frac);

   size_t location() const {return m_driver_location;}
   int location_frac() const {return m_location_frac;}

   bool is_varying() const override;

private:
   void evaluate_spi_sid();

   virtual void set_color_ioinfo(r600_shader_io& io) const;
   void set_specific_ioinfo(r600_shader_io& io) const override;
   size_t m_driver_location;
   int m_location_frac;
   int m_sid;
   int m_spi_sid;
   tgsi_interpolate_mode m_interpolate;
   tgsi_interpolate_loc m_interpolate_loc;
   int m_ij_index;
   int m_lds_pos;
   int m_mask;
};

class ShaderInputColor: public ShaderInputVarying {
public:
   ShaderInputColor(tgsi_semantic _name, int sid, unsigned driver_location,
                    unsigned frac, unsigned components, tgsi_interpolate_mode interpolate,
                    tgsi_interpolate_loc interp_loc);
   ShaderInputColor(tgsi_semantic name, int sid, nir_variable *input);
   void set_back_color(unsigned back_color_input_idx);
   unsigned back_color_input_index() const {
      return m_back_color_input_idx;
   }
private:
   void set_color_ioinfo(UNUSED r600_shader_io& io) const override;
   unsigned m_back_color_input_idx;

};

class ShaderIO
{
public:
   ShaderIO();

   size_t add_input(ShaderInput *input);

   std::vector<PShaderInput>& inputs();
   ShaderInput& input(size_t k);

   ShaderInput& input(size_t driver_loc, int frac);

   void set_two_sided();
   bool two_sided() {return m_two_sided;}

   int nlds() const  {
      return m_lds_pos;
   }

   void sort_varying_inputs();

   size_t size() const {return m_inputs.size();}

   PShaderInput find_varying(tgsi_semantic name, int sid);

   void update_lds_pos();

private:
   std::vector<PShaderInput> m_inputs;
   std::vector<int> m_ldspos;
   bool m_two_sided;
   int m_lds_pos;

};

std::pair<unsigned, unsigned>
r600_get_varying_semantic(unsigned varying_location);


}

#endif // SFN_SHADERIO_H
