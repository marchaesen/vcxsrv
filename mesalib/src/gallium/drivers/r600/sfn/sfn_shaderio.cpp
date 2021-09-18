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

#include "sfn_shaderio.h"
#include "sfn_debug.h"
#include "tgsi/tgsi_from_mesa.h"

#include <queue>

namespace r600 {

using std::vector;
using std::priority_queue;

ShaderIO::ShaderIO():
   m_two_sided(false),
   m_lds_pos(0)
{

}

ShaderInput::ShaderInput(tgsi_semantic name):
   m_name(name),
   m_gpr(0),
   m_uses_interpolate_at_centroid(false)
{
}

ShaderInput::~ShaderInput()
{
}

void ShaderInput::set_lds_pos(UNUSED int lds_pos)
{
}

int ShaderInput::ij_index() const
{
   return -1;
}

bool ShaderInput::interpolate() const
{
   return false;
}

int ShaderInput::lds_pos() const
{
   return 0;
}

bool ShaderInput::is_varying() const
{
   return false;
}

void ShaderInput::set_uses_interpolate_at_centroid()
{
   m_uses_interpolate_at_centroid = true;
}

void ShaderInput::set_ioinfo(r600_shader_io& io, int translated_ij_index) const
{
   io.name = m_name;
   io.gpr = m_gpr;
   io.ij_index = translated_ij_index;
   io.lds_pos = lds_pos();
   io.uses_interpolate_at_centroid = m_uses_interpolate_at_centroid;

   set_specific_ioinfo(io);
}

void ShaderInput::set_specific_ioinfo(UNUSED r600_shader_io& io) const
{
}

ShaderInputSystemValue::ShaderInputSystemValue(tgsi_semantic name, int gpr):
   ShaderInput(name),
   m_gpr(gpr)
{
}

void ShaderInputSystemValue::set_specific_ioinfo(r600_shader_io& io) const
{
   io.gpr = m_gpr;
   io.ij_index = 0;
}

ShaderInputVarying::ShaderInputVarying(tgsi_semantic _name, int sid, unsigned driver_location,
                                       unsigned frac, unsigned components,
                                       tgsi_interpolate_mode interpolate,
                                       tgsi_interpolate_loc interp_loc):
   ShaderInput(_name),
   m_driver_location(driver_location),
   m_location_frac(frac),
   m_sid(sid),
   m_interpolate(interpolate),
   m_interpolate_loc(interp_loc),
   m_ij_index(-10),
   m_lds_pos(0),
   m_mask(((1 << components) - 1) << frac)
{
   evaluate_spi_sid();

   m_ij_index = interpolate == TGSI_INTERPOLATE_LINEAR ? 3 : 0;
   switch (interp_loc) {
   case TGSI_INTERPOLATE_LOC_CENTROID: m_ij_index += 2; break;
   case TGSI_INTERPOLATE_LOC_CENTER: m_ij_index += 1; break;
   default:
      ;
   }
}

ShaderInputVarying::ShaderInputVarying(tgsi_semantic _name, int sid, nir_variable *input):
   ShaderInput(_name),
   m_driver_location(input->data.driver_location),
   m_location_frac(input->data.location_frac),
   m_sid(sid),
   m_ij_index(-10),
   m_lds_pos(0),
   m_mask(((1 << input->type->components()) - 1) << input->data.location_frac)
{
   sfn_log << SfnLog::io << __func__
           << "name:" << _name
           << " sid: " << sid
           << " op: " << input->data.interpolation;

   evaluate_spi_sid();

   enum glsl_base_type base_type =
      glsl_get_base_type(glsl_without_array(input->type));

   switch (input->data.interpolation) {
   case INTERP_MODE_NONE:
      if (glsl_base_type_is_integer(base_type)) {
         m_interpolate = TGSI_INTERPOLATE_CONSTANT;
         break;
      }

      if (name() == TGSI_SEMANTIC_COLOR) {
         m_interpolate = TGSI_INTERPOLATE_COLOR;
         m_ij_index = 0;
         break;
      }
      FALLTHROUGH;

   case INTERP_MODE_SMOOTH:
      assert(!glsl_base_type_is_integer(base_type));

      m_interpolate = TGSI_INTERPOLATE_PERSPECTIVE;
      m_ij_index = 0;
      break;

   case INTERP_MODE_NOPERSPECTIVE:
      assert(!glsl_base_type_is_integer(base_type));

      m_interpolate = TGSI_INTERPOLATE_LINEAR;
      m_ij_index = 3;
      break;

   case INTERP_MODE_FLAT:
      m_interpolate = TGSI_INTERPOLATE_CONSTANT;
      break;

   default:
      m_interpolate = TGSI_INTERPOLATE_CONSTANT;
      break;
   }

   if (input->data.sample) {
      m_interpolate_loc = TGSI_INTERPOLATE_LOC_SAMPLE;
   } else if (input->data.centroid) {
      m_interpolate_loc = TGSI_INTERPOLATE_LOC_CENTROID;
      m_ij_index += 2;
   } else {
      m_interpolate_loc = TGSI_INTERPOLATE_LOC_CENTER;
      m_ij_index += 1;
   }
   sfn_log << SfnLog::io
           << " -> IP:" << m_interpolate
           << " IJ:" << m_ij_index
           << "\n";
}

bool ShaderInputVarying::is_varying() const
{
   return true;
}

void ShaderInputVarying::update_mask(int additional_comps, int frac)
{
   m_mask |= ((1 << additional_comps) - 1) << frac;
}

void ShaderInputVarying::evaluate_spi_sid()
{
   switch (name()) {
   case TGSI_SEMANTIC_PSIZE:
   case TGSI_SEMANTIC_EDGEFLAG:
   case TGSI_SEMANTIC_FACE:
   case TGSI_SEMANTIC_SAMPLEMASK:
      assert(0 && "System value used as varying");
      break;
   case TGSI_SEMANTIC_POSITION:
      m_spi_sid = 0;
      break;
   case TGSI_SEMANTIC_GENERIC:
   case TGSI_SEMANTIC_TEXCOORD:
   case TGSI_SEMANTIC_PCOORD:
      m_spi_sid = m_sid + 1;
      break;
   default:
      /* For non-generic params - pack name and sid into 8 bits */
      m_spi_sid = (0x80 | (name() << 3) | m_sid) + 1;
   }
}

ShaderInputVarying::ShaderInputVarying(tgsi_semantic name,
                                       const ShaderInputVarying& orig, size_t location):
   ShaderInput(name),
   m_driver_location(location),
   m_location_frac(orig.location_frac()),

   m_sid(orig.m_sid),
   m_spi_sid(orig.m_spi_sid),
   m_interpolate(orig.m_interpolate),
   m_interpolate_loc(orig.m_interpolate_loc),
   m_ij_index(orig.m_ij_index),
   m_lds_pos(0),
   m_mask(0)
{
   evaluate_spi_sid();
}

bool ShaderInputVarying::interpolate() const
{
   return m_interpolate > 0;
}

int ShaderInputVarying::ij_index() const
{
   return m_ij_index;
}

void ShaderInputVarying::set_lds_pos(int lds_pos)
{
   m_lds_pos = lds_pos;
}

int ShaderInputVarying::lds_pos() const
{
   return m_lds_pos;
}

void ShaderInputVarying::set_specific_ioinfo(r600_shader_io& io) const
{
   io.interpolate = m_interpolate;
   io.interpolate_location = m_interpolate_loc;
   io.sid = m_sid;
   io.spi_sid = m_spi_sid;
   set_color_ioinfo(io);
}

void ShaderInputVarying::set_color_ioinfo(UNUSED r600_shader_io& io) const
{
   sfn_log << SfnLog::io << __func__ << " Don't set color_ioinfo\n";
}

ShaderInputColor::ShaderInputColor(tgsi_semantic name, int sid, nir_variable *input):
   ShaderInputVarying(name, sid, input),
   m_back_color_input_idx(0)
{
   sfn_log << SfnLog::io << __func__ << "name << " << name << " sid << " << sid << "\n";
}

ShaderInputColor::ShaderInputColor(tgsi_semantic _name, int sid, unsigned driver_location,
                                   unsigned frac, unsigned components, tgsi_interpolate_mode interpolate,
                                   tgsi_interpolate_loc interp_loc):
   ShaderInputVarying(_name, sid, driver_location,frac, components, interpolate, interp_loc),
   m_back_color_input_idx(0)
{
   sfn_log << SfnLog::io << __func__ << "name << " << _name << " sid << " << sid << "\n";
}

void ShaderInputColor::set_back_color(unsigned back_color_input_idx)
{
   sfn_log << SfnLog::io << "Set back color index " << back_color_input_idx << "\n";
   m_back_color_input_idx = back_color_input_idx;
}

void ShaderInputColor::set_color_ioinfo(r600_shader_io& io) const
{
   sfn_log << SfnLog::io << __func__ << " set color_ioinfo " << m_back_color_input_idx << "\n";
   io.back_color_input = m_back_color_input_idx;
}

size_t ShaderIO::add_input(ShaderInput *input)
{
   m_inputs.push_back(PShaderInput(input));
   return m_inputs.size() - 1;
}

PShaderInput ShaderIO::find_varying(tgsi_semantic name, int sid)
{
   for (auto& a : m_inputs) {
      if (a->name() == name) {
         assert(a->is_varying());
         auto& v = static_cast<ShaderInputVarying&>(*a);
         if (v.sid() == sid)
            return a;
      }
   }
   return nullptr;
}

struct VaryingShaderIOLess {
   bool operator () (PShaderInput lhs, PShaderInput rhs) const
   {
      const ShaderInputVarying& l = static_cast<ShaderInputVarying&>(*lhs);
      const ShaderInputVarying& r = static_cast<ShaderInputVarying&>(*rhs);
      return l.location() > r.location();
   }
};

void ShaderIO::sort_varying_inputs()
{
   priority_queue<PShaderInput, vector<PShaderInput>, VaryingShaderIOLess> q;

   vector<int> idx;

   for (auto i = 0u; i < m_inputs.size(); ++i) {
      if (m_inputs[i]->is_varying()) {
         q.push(m_inputs[i]);
         idx.push_back(i);
      }
   }

   auto next_index = idx.begin();
   while (!q.empty()) {
      auto si = q.top();
      q.pop();
      m_inputs[*next_index++] = si;
   }
}

void ShaderIO::update_lds_pos()
{
   m_lds_pos = -1;
   m_ldspos.resize(m_inputs.size());
   for (auto& i : m_inputs) {
      if (!i->is_varying())
         continue;

      auto& v = static_cast<ShaderInputVarying&>(*i);
      /* There are shaders that miss an input ...*/
      if (m_ldspos.size() <= static_cast<unsigned>(v.location()))
          m_ldspos.resize(v.location() + 1);
   }

   std::fill(m_ldspos.begin(), m_ldspos.end(), -1);
   for (auto& i : m_inputs) {
      if (!i->is_varying())
         continue;

      auto& v = static_cast<ShaderInputVarying&>(*i);
      if (v.name() == TGSI_SEMANTIC_POSITION)
         continue;

      if (m_ldspos[v.location()] < 0) {
         ++m_lds_pos;
         m_ldspos[v.location()] = m_lds_pos;
      }
      v.set_lds_pos(m_lds_pos);
   }
   ++m_lds_pos;
}

std::vector<PShaderInput> &ShaderIO::inputs()
{
   return m_inputs;
}

ShaderInput& ShaderIO::input(size_t k)
{
   assert(k < m_inputs.size());
   return *m_inputs[k];
}

ShaderInput& ShaderIO::input(size_t driver_loc, int frac)
{
   for (auto& i: m_inputs) {
      if (!i->is_varying())
         continue;

      auto& v = static_cast<ShaderInputVarying&>(*i);
      if (v.location() == driver_loc)
         return v;
   }
   return input(driver_loc);
}

void ShaderIO::set_two_sided()
{
   m_two_sided = true;
}

std::pair<unsigned, unsigned>
r600_get_varying_semantic(unsigned varying_location)
{
   std::pair<unsigned, unsigned> result;
   tgsi_get_gl_varying_semantic(static_cast<gl_varying_slot>(varying_location),
                                true, &result.first, &result.second);

   if (result.first == TGSI_SEMANTIC_GENERIC) {
      result.second += 9;
   } else if (result.first == TGSI_SEMANTIC_PCOORD) {
      result.second = 8;
   }
   return result;
}



}

