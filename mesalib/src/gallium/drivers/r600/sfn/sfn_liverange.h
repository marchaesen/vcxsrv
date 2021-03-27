/* -*- mesa-c++  -*-
 *
 * Copyright (c) 2018-2019 Collabora LTD
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

#ifndef SFN_LIVERANGE_H
#define SFN_LIVERANGE_H

#include <cstdint>
#include <ostream>
#include <vector>
#include <limits>

#include "sfn_instruction_base.h"
#include "sfn_nir.h"

namespace r600 {

/** Storage to record the required live range of a temporary register
 * begin == end == -1 indicates that the register can be reused without
 * limitations. Otherwise, "begin" indicates the first instruction in which
 * a write operation may target this temporary, and end indicates the
 * last instruction in which a value can be read from this temporary.
 * Hence, a register R2 can be merged with a register R1 if R1.end <= R2.begin.
 */
struct register_live_range {
   int begin;
   int end;
   bool is_array_elm;
};

enum prog_scope_type {
   outer_scope,           /* Outer program scope */
   loop_body,             /* Inside a loop */
   if_branch,             /* Inside if branch */
   else_branch,           /* Inside else branch */
   switch_body,           /* Inside switch statement */
   switch_case_branch,    /* Inside switch case statement */
   switch_default_branch, /* Inside switch default statement */
   undefined_scope
};

class prog_scope {
public:
   prog_scope();
   prog_scope(prog_scope *parent, prog_scope_type type, int id,
              int depth, int begin);

   prog_scope_type type() const;
   prog_scope *parent() const;
   int nesting_depth() const;
   int id() const;
   int end() const;
   int begin() const;
   int loop_break_line() const;

   const prog_scope *in_else_scope() const;
   const prog_scope *in_ifelse_scope() const;
   const prog_scope *in_parent_ifelse_scope() const;
   const prog_scope *innermost_loop() const;
   const prog_scope *outermost_loop() const;
   const prog_scope *enclosing_conditional() const;

   bool is_loop() const;
   bool is_in_loop() const;
   bool is_switchcase_scope_in_loop() const;
   bool is_conditional() const;
   bool is_child_of(const prog_scope *scope) const;
   bool is_child_of_ifelse_id_sibling(const prog_scope *scope) const;

   bool break_is_for_switchcase() const;
   bool contains_range_of(const prog_scope& other) const;

   void set_end(int end);
   void set_loop_break_line(int line);

private:
   prog_scope_type scope_type;
   int scope_id;
   int scope_nesting_depth;
   int scope_begin;
   int scope_end;
   int break_loop_line;
   prog_scope *parent_scope;
};

/* Some storage class to encapsulate the prog_scope (de-)allocations */
class prog_scope_storage {
public:
   prog_scope_storage(int n);
   ~prog_scope_storage();
   prog_scope * create(prog_scope *p, prog_scope_type type, int id,
                       int lvl, int s_begin);
private:
   int current_slot;
   std::vector<prog_scope> storage;
};

/* Class to track the access to a component of a temporary register. */

class temp_comp_access {
public:
   temp_comp_access();

   void record_read(int line, prog_scope *scope);
   void record_write(int line, prog_scope *scope);
   register_live_range get_required_live_range();
private:
   void propagate_live_range_to_dominant_write_scope();
   bool conditional_ifelse_write_in_loop() const;

   void record_ifelse_write(const prog_scope& scope);
   void record_if_write(const prog_scope& scope);
   void record_else_write(const prog_scope& scope);

   prog_scope *last_read_scope;
   prog_scope *first_read_scope;
   prog_scope *first_write_scope;

   int first_write;
   int last_read;
   int last_write;
   int first_read;

   /* This member variable tracks the current resolution of conditional writing
    * to this temporary in IF/ELSE clauses.
    *
    * The initial value "conditionality_untouched" indicates that this
    * temporary has not yet been written to within an if clause.
    *
    * A positive (other than "conditionality_untouched") number refers to the
    * last loop id for which the write was resolved as unconditional. With each
    * new loop this value will be overwitten by "conditionality_unresolved"
    * on entering the first IF clause writing this temporary.
    *
    * The value "conditionality_unresolved" indicates that no resolution has
    * been achieved so far. If the variable is set to this value at the end of
    * the processing of the whole shader it also indicates a conditional write.
    *
    * The value "write_is_conditional" marks that the variable is written
    * conditionally (i.e. not in all relevant IF/ELSE code path pairs) in at
    * least one loop.
    */
   int conditionality_in_loop_id;

   /* Helper constants to make the tracking code more readable. */
   static const int write_is_conditional = -1;
   static const int conditionality_unresolved = 0;
   static const int conditionality_untouched;
   static const int write_is_unconditional;

   /* A bit field tracking the nexting levels of if-else clauses where the
    * temporary has (so far) been written to in the if branch, but not in the
    * else branch.
    */
   unsigned int if_scope_write_flags;

   int next_ifelse_nesting_depth;
   static const int supported_ifelse_nesting_depth = 32;

   /* Tracks the last if scope in which the temporary was written to
    * without a write in the corresponding else branch. Is also used
    * to track read-before-write in the according scope.
    */
   const prog_scope *current_unpaired_if_write_scope;

   /* Flag to resolve read-before-write in the else scope. */
   bool was_written_in_current_else_scope;
};

/* Class to track the access to all components of a temporary register. */
class temp_access {
public:
   temp_access();
   void record_read(int line, prog_scope *scope, int swizzle, bool is_array_elm);
   void record_write(int line, prog_scope *scope, int writemask, bool is_array_elm);
   register_live_range get_required_live_range();
private:
   void update_access_mask(int mask);

   temp_comp_access comp[4];
   int access_mask;
   bool needs_component_tracking;
   bool is_array_element;
};

/* Helper class to merge the live ranges of an arrays.
 *
 * For arrays the array length, live range, and component access needs to
 * be kept, because when live ranges are merged or arrays are interleaved
 * one can only merge or interleave an array into another with equal or more
 * elements. For interleaving it is also required that the sum of used swizzles
 * is at most four.
 */

class array_live_range {
public:
   array_live_range();
   array_live_range(unsigned aid, unsigned alength);
   array_live_range(unsigned aid, unsigned alength, int first_access,
		  int last_access, int mask);

   void set_live_range(int first_access, int last_access);
   void set_begin(int _begin){first_access = _begin;}
   void set_end(int _end){last_access = _end;}
   void set_access_mask(int s);

   static void merge(array_live_range *a, array_live_range *b);
   static void interleave(array_live_range *a, array_live_range *b);

   int array_id() const {return id;}
   int target_array_id() const {return target_array ? target_array->id : 0;}
   const array_live_range *final_target() const {return target_array ?
	       target_array->final_target() : this;}
   unsigned array_length() const { return length;}
   int begin() const { return first_access;}
   int end() const { return last_access;}
   int access_mask() const { return component_access_mask;}
   int used_components() const {return used_component_count;}

   bool time_doesnt_overlap(const array_live_range& other) const;

   void print(std::ostream& os) const;

   bool is_mapped() const { return target_array != nullptr;}

   int8_t remap_one_swizzle(int8_t idx) const;

private:
   void init_swizzles();
   void set_target(array_live_range  *target);
   void merge_live_range_from(array_live_range *other);
   void interleave_into(array_live_range *other);

   unsigned id;
   unsigned length;
   int first_access;
   int last_access;
   uint8_t component_access_mask;
   uint8_t used_component_count;
   array_live_range *target_array;
   int8_t swizzle_map[4];
};



class LiverangeEvaluator {
public:
   LiverangeEvaluator();

   void run(const Shader& shader,
            std::vector<register_live_range> &register_live_ranges);

   void scope_if();
   void scope_else();
   void scope_endif();
   void scope_loop_begin();
   void scope_loop_end();
   void scope_loop_break();

   void record_read(const Value& src, bool is_array_elm = false);
   void record_write(const Value& dst, bool is_array_elm = false);

   void record_read(const GPRVector& src);
   void record_write(const GPRVector& dst);

private:

   prog_scope *create_scope(prog_scope *parent, prog_scope_type type, int id,
                            int lvl, int s_begin);


   void get_required_live_ranges(std::vector<register_live_range>& register_live_ranges);

   int line;
   int loop_id;
   int if_id;
   int switch_id;
   bool is_at_end;
   int n_scopes;
   std::unique_ptr<prog_scope_storage> scopes;
   prog_scope *cur_scope;

   std::vector<temp_access> temp_acc;

};

std::vector<rename_reg_pair>
get_temp_registers_remapping(const std::vector<register_live_range>& live_ranges);

} // end namespace r600

#endif
