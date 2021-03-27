/* -*- mesa-c++  -*-
 *
 * Copyright (c) 2019 Collabora LTD
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

#include "util/u_debug.h"
#include "sfn_debug.h"

namespace r600 {

class stderr_streambuf : public std::streambuf
{
public:
   stderr_streambuf();
protected:
   int sync();
   int overflow(int c);
   std::streamsize xsputn ( const char *s, std::streamsize n );
};

stderr_streambuf::stderr_streambuf()
{

}

int stderr_streambuf::sync()
{
   fflush(stderr);
   return 0;
}

int stderr_streambuf::overflow(int c)
{
   fputc(c, stderr);
   return 0;
}

static const struct debug_named_value sfn_debug_options[] = {
   {"instr", SfnLog::instr, "Log all consumed nir instructions"},
   {"ir", SfnLog::r600ir, "Log created R600 IR"},
   {"cc", SfnLog::cc, "Log R600 IR to assembly code creation"},
   {"noerr", SfnLog::err, "Don't log shader conversion errors"},
   {"si", SfnLog::shader_info, "Log shader info (non-zero values)"},
   {"ts", SfnLog::test_shader, "Log shaders in tests"},
   {"reg", SfnLog::reg, "Log register allocation and lookup"},
   {"io", SfnLog::io, "Log shader in and output"},
   {"ass", SfnLog::assembly, "Log IR to assembly conversion"},
   {"flow", SfnLog::flow, "Log Flow instructions"},
   {"merge", SfnLog::merge, "Log register merge operations"},
   {"nomerge", SfnLog::nomerge, "Skip register merge step"},
   {"tex", SfnLog::tex, "Log texture ops"},
   {"trans", SfnLog::trans, "Log generic translation messages"},
   DEBUG_NAMED_VALUE_END
};

SfnLog sfn_log;

std::streamsize stderr_streambuf::xsputn ( const char *s, std::streamsize n )
{
   std::streamsize i = n;
   while (i--)
      fputc(*s++, stderr);
   return n;
}

SfnLog::SfnLog():
   m_active_log_flags(0),
   m_log_mask(0),
   m_output(new stderr_streambuf())
{
   m_log_mask = debug_get_flags_option("R600_NIR_DEBUG", sfn_debug_options, 0);
   m_log_mask ^= err;
}

SfnLog& SfnLog::operator << (SfnLog::LogFlag const l)
{
   m_active_log_flags = l;
   return *this;
}

SfnLog& SfnLog::operator << (UNUSED std::ostream & (*f)(std::ostream&))
{
   if (m_active_log_flags & m_log_mask)
      m_output << f;
   return *this;
}

SfnLog& SfnLog::operator << (nir_shader& sh)
{
   if (m_active_log_flags & m_log_mask)
      nir_print_shader(&sh, stderr);
   return *this;
}

SfnLog& SfnLog::operator << (nir_instr &instr)
{
   if (m_active_log_flags & m_log_mask)
      nir_print_instr(&instr, stderr);
   return *this;
}

SfnTrace::SfnTrace(SfnLog::LogFlag flag, const char *msg):
   m_flag(flag),
   m_msg(msg)
{
   sfn_log << m_flag << std::string(" ", 2 * m_indention++)
           << "BEGIN: " << m_msg << "\n";
}

SfnTrace::~SfnTrace()
{
   sfn_log << m_flag << std::string(" ", 2 * m_indention--)
           << "END:   " << m_msg << "\n";
}

int SfnTrace::m_indention = 0;

}
