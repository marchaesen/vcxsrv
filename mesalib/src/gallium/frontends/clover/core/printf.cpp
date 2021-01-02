//
// Copyright 2020 Serge Martin
//
// Permission is hereby granted, free of charge, to any person obtaining a
// copy of this software and associated documentation files (the "Software"),
// to deal in the Software without restriction, including without limitation
// the rights to use, copy, modify, merge, publish, distribute, sublicense,
// and/or sell copies of the Software, and to permit persons to whom the
// Software is furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
// THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
// OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
// ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
// OTHER DEALINGS IN THE SOFTWARE.
//

#include <cstring>
#include <cstdio>
#include <string>
#include <iostream>

#include "util/u_math.h"
#include "core/printf.hpp"

#include "util/u_printf.h"
using namespace clover;

namespace {

   const cl_uint hdr_dwords = 2;
   const cl_uint initial_buffer_offset = hdr_dwords * sizeof(cl_uint);

   /* all valid chars that can appear in CL C printf string. */
   const std::string clc_printf_whitelist = "%0123456789-+ #.AacdeEfFgGhilopsuvxX";

   void
   print_formatted(const std::vector<module::printf_info> &formatters,
                   bool _strings_in_buffer,
                   const std::vector<char> &buffer) {

      static std::atomic<unsigned> warn_count;
      if (buffer.empty() && !warn_count++)
	 std::cerr << "Printf used but no printf occurred - may cause perfomance issue." << std::endl;

      for (size_t buf_pos = 0; buf_pos < buffer.size(); ) {
         cl_uint fmt_idx = *(cl_uint*)&buffer[buf_pos];
         assert(fmt_idx > 0);
         module::printf_info fmt = formatters[fmt_idx-1];

         std::string format = (char *)fmt.strings.data();
         buf_pos += sizeof(cl_uint);

         if (fmt.arg_sizes.empty()) {
            printf("%s", format.c_str());

         } else {
            size_t fmt_last_pos = 0;
            size_t fmt_pos = 0;
            for (int arg_size : fmt.arg_sizes) {
               const size_t spec_pos = util_printf_next_spec_pos(format, fmt_pos);
               const size_t cur_tok = format.rfind('%', spec_pos);
               const size_t next_spec = util_printf_next_spec_pos(format, spec_pos);
               const size_t next_tok = next_spec == std::string::npos ? std::string::npos :
                  format.rfind('%', next_spec);

               size_t vec_pos = format.find_first_of("v", cur_tok + 1);
               size_t mod_pos = format.find_first_of("hl", cur_tok + 1);

               // print the part before the format token
               if (cur_tok != fmt_last_pos) {
                  std::string s = format.substr(fmt_last_pos,
                                                cur_tok - fmt_last_pos);
                  printf("%s", s.c_str());
               }

               std::string print_str;
               print_str = format.substr(cur_tok, spec_pos + 1 - cur_tok);

               /* Never pass a 'n' spec to the host printf */
               bool valid_str = print_str.find_first_not_of(clc_printf_whitelist) ==
                  std::string::npos;

               // print the formated part
               if (spec_pos != std::string::npos && valid_str) {
                  bool is_vector = vec_pos != std::string::npos &&
                     vec_pos + 1 < spec_pos;
                  bool is_string = format[spec_pos] == 's';
                  bool is_float = std::string("fFeEgGaA")
                     .find(format[spec_pos]) != std::string::npos;

                  if (is_string) {
                     if (_strings_in_buffer)
                        printf(print_str.c_str(), &buffer[buf_pos]);
                     else {
                        uint64_t idx;
                        memcpy(&idx, &buffer[buf_pos], 8);
                        printf(print_str.c_str(), &fmt.strings[idx]);
                     }
                  } else {
                     int component_count = 1;

                     if (is_vector) {
                        size_t l = std::min(mod_pos, spec_pos) - vec_pos - 1;
                        std::string s = format.substr(vec_pos + 1, l);
                        component_count = std::stoi(s);
                        if (mod_pos != std::string::npos) {
                           // CL C has hl specifier for 32-bit vectors, C doesn't have it
                           // just remove it.
                           std::string mod = format.substr(mod_pos, 2);
                           if (mod == "hl")
                              mod_pos = std::string::npos;
			}
                        print_str.erase(vec_pos - cur_tok, std::min(mod_pos, spec_pos) - vec_pos);
                        print_str.push_back(',');
                     }

                     //in fact vec3 are vec4
                     int men_components =
                        component_count == 3 ? 4 : component_count;
                     size_t elmt_size = arg_size / men_components;

                     for (int i = 0; i < component_count; i++) {
                        size_t elmt_buf_pos = buf_pos + i * elmt_size;
                        if (is_vector && i + 1 == component_count)
                           print_str.pop_back();

                        if (is_float) {
                           switch (elmt_size) {
                           case 2:
                              cl_half h;
                              std::memcpy(&h, &buffer[elmt_buf_pos], elmt_size);
                              printf(print_str.c_str(), h);
                              break;
                           case 4:
                              cl_float f;
                              std::memcpy(&f, &buffer[elmt_buf_pos], elmt_size);
                              printf(print_str.c_str(), f);
                              break;
                           default:
                              cl_double d;
                              std::memcpy(&d, &buffer[elmt_buf_pos], elmt_size);
                              printf(print_str.c_str(), d);
                           }
                        } else {
                           cl_long l = 0;
                           std::memcpy(&l, &buffer[elmt_buf_pos], elmt_size);
                           printf(print_str.c_str(), l);
                        }
                     }
                  }
                  // print the remaining
                  if (next_tok != spec_pos) {
                     std::string s = format.substr(spec_pos + 1,
                                                   next_tok - spec_pos - 1);
                     printf("%s", s.c_str());
                  }
               }

               fmt_pos = spec_pos;
               fmt_last_pos = next_tok;

               buf_pos += arg_size;
               buf_pos = ALIGN(buf_pos, 4);
            }
         }
      }
   }
}

std::unique_ptr<printf_handler>
printf_handler::create(const intrusive_ptr<command_queue> &q,
                       const std::vector<module::printf_info> &infos,
                       bool strings_in_buffer,
                       cl_uint size) {
   return std::unique_ptr<printf_handler>(
                                       new printf_handler(q, infos, strings_in_buffer, size));
}

printf_handler::printf_handler(const intrusive_ptr<command_queue> &q,
                               const std::vector<module::printf_info> &infos,
                               bool strings_in_buffer,
                               cl_uint size) :
   _q(q), _formatters(infos), _strings_in_buffer(strings_in_buffer), _size(size), _buffer() {

   if (_size) {
      std::string data;
      data.reserve(_size);
      cl_uint header[2] = { 0 };

      header[0] = initial_buffer_offset;
      header[1] = _size;

      data.append((char *)header, (char *)(header+hdr_dwords));
      _buffer = std::unique_ptr<root_buffer>(new root_buffer(_q->context,
                                             std::vector<cl_mem_properties>(),
                                             CL_MEM_COPY_HOST_PTR,
                                             _size, (char*)data.data()));
   }
}

cl_mem
printf_handler::get_mem() {
   return (cl_mem)(_buffer.get());
}

void
printf_handler::print() {
   if (!_buffer)
      return;

   mapping src = { *_q, _buffer->resource_in(*_q), CL_MAP_READ, true,
                  {{ 0 }}, {{ _size, 1, 1 }} };

   cl_uint header[2] = { 0 };
   std::memcpy(header,
               static_cast<const char *>(src),
               initial_buffer_offset);

   cl_uint buffer_size = header[0];
   buffer_size -= initial_buffer_offset;
   std::vector<char> buf;
   buf.resize(buffer_size);

   std::memcpy(buf.data(),
               static_cast<const char *>(src) + initial_buffer_offset,
               buffer_size);

   // mixed endian isn't going to work, sort it out if anyone cares later.
   assert(_q->device().endianness() == PIPE_ENDIAN_NATIVE);
   print_formatted(_formatters, _strings_in_buffer, buf);
}
