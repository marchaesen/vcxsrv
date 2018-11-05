/*
 * Copyright © 2017 Gert Wollny
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

/* A short overview on how the array merging works:
 *
 * Inputs:
 *   - per array information: live range, access mask, size
 *   - the program
 *
 * Output:
 *   - the program with updated array addressing
 *
 * Pseudo algorithm:
 *
 * repeat
 *    for all pairs of arrays:
 *       if they have non-overlapping live ranges and equal access masks:
 *          - pick shorter array
 *          - merge its live range into the longer array
 *          - set its merge target array to the longer array
 *          - mark the shorter array as processed
 *
 *    for all pairs of arrays:
 *       if they have overlapping live ranges use in sum at most four components:
 *          - pick shorter array
 *          - evaluate reswizzle map to move its components into the components
 *            that are not used by the longer array
 *          - set its merge target array to the longer array
 *          - mark the shorter array as processed
 *          - bail out loop
 *  until no more successfull merges were found
 *
 *  for all pairs of arrays:
 *     if they have non-overlapping live ranges:
 *          - pick shorter array
 *          - merge its live range into the longer array
 *          - set its merge target array to the longer array
 *          - mark the shorter array as processed
 *
 * Finalize remapping map so that target arrays are always final, i.e. have
 * themselfes no merge target set.
 *
 * Example:
 *   ID  | Length | Live range | access mask | target id | reswizzle
 *   ================================================================
 *   1       3       3-10          x___            0        ____
 *   2       4      13-20          x___            0        ____
 *   3       8       3-20          x___            0        ____
 *   4       6      21-40          xy__            0        ____
 *   5       7      12-30          xy__            0        ____
 *
 * 1. merge live ranges 1 and 2
 *
 *   ID  | Length | Live range | access mask | target id | reswizzle
 *   ================================================================
 *   1       -        -            x___            2        ____
 *   2       4       3-20          x___            0        ____
 *   3       8       3-20          x___            0        ____
 *   4       6      21-40          xy__            0        ____
 *   5       7      12-30          xy__            0        ____
 *
 *
 *  3. interleave 2 and 3
 *
 *   ID  | Length | Live range | access mask | target id | reswizzle
 *   ================================================================
 *   1       -        -            x___            2        ____
 *   2       -        -            x___            3        _x__
 *   3       8       3-20          xy__            0        ____
 *   4       6      21-40          xy__            0        ____
 *   5       7      12-30          xy__            0        ____
 *
 *   3. merge live ranges 3 and 4
 *
 *   ID  | Length | Live range | access mask | target id | reswizzle
 *   ================================================================
 *   1       -        -            x___            2        ____
 *   2       -        -            x___            3        _x__
 *   3       8       3-40          xy__            0        ____
 *   4       -        -            xy__            3        ____
 *   5       7       3-21          xy__            0        ____
 *
 *   4. interleave 3 and 5
 *
 *   ID  | Length | Live range | access mask | target id | reswizzle
 *   ================================================================
 *   1       -        -            x___            2        ____
 *   2       -        -            x___            3        _x__
 *   3       8       3-40          xy__            0        ____
 *   4       -        -            xy__            3        ____
 *   5       -        -            xy__            3        __xy
 *
 *   5. finalize remapping
 *   (Array 1 has been merged with 2 that was later interleaved, so
 *   the reswizzeling must be propagated.
 *
 *   ID  | Length | Live range | new access mask | target id | reswizzle
 *   ================================================================
 *   1       -        -               _y__            3        _x__
 *   2       -        -               _y__            3        _x__
 *   3       8       3-40             xy__            0        ____
 *   4       -        -               xy__            3        ____
 *   5       -        -               __zw            3        __xy
 *
*/

#include "program/prog_instruction.h"
#include "util/u_math.h"
#include <ostream>
#include <cassert>
#include <algorithm>

#include <iostream>

#include "st_glsl_to_tgsi_array_merge.h"

#if __cplusplus >= 201402L
#include <memory>
using std::unique_ptr;
using std::make_unique;
#endif

#define ARRAY_MERGE_DEBUG 0

#if ARRAY_MERGE_DEBUG > 0
#define ARRAY_MERGE_DUMP(x) do std::cerr << x; while (0)
#define ARRAY_MERGE_DUMP_BLOCK(x) do { x } while (0)
#else
#define ARRAY_MERGE_DUMP(x)
#define ARRAY_MERGE_DUMP_BLOCK(x)
#endif

static const char xyzw[] = "xyzw";

array_live_range::array_live_range():
   id(0),
   length(0),
   first_access(0),
   last_access(0),
   component_access_mask(0),
   used_component_count(0),
   target_array(nullptr)
{
   init_swizzles();
}

array_live_range::array_live_range(unsigned aid, unsigned alength):
   id(aid),
   length(alength),
   first_access(0),
   last_access(0),
   component_access_mask(0),
   used_component_count(0),
   target_array(nullptr)
{
   init_swizzles();
}

array_live_range::array_live_range(unsigned aid, unsigned alength, int begin,
				   int end, int sw):
   id(aid),
   length(alength),
   first_access(begin),
   last_access(end),
   component_access_mask(sw),
   used_component_count(util_bitcount(sw)),
   target_array(nullptr)
{
   init_swizzles();
}

void array_live_range::init_swizzles()
{
   for (int i = 0; i < 4; ++i)
      swizzle_map[i] = i;
}

void array_live_range::set_live_range(int _begin, int _end)
{
   set_begin(_begin);
   set_end(_end);
}

void array_live_range::set_access_mask(int mask)
{
   component_access_mask = mask;
   used_component_count = util_bitcount(mask);
}

void array_live_range::merge(array_live_range *a, array_live_range *b)
{
    if (a->array_length() < b->array_length())
       b->merge_live_range_from(a);
    else
       a->merge_live_range_from(b);
}

void array_live_range::interleave(array_live_range *a, array_live_range *b)
{
    if (a->array_length() < b->array_length())
       a->interleave_into(b);
    else
       b->interleave_into(a);
}

void array_live_range::interleave_into(array_live_range *other)
{
   for (int i = 0; i < 4; ++i) {
      swizzle_map[i] = -1;
   }

   int trgt_access_mask = other->access_mask();
   int summary_access_mask = trgt_access_mask;
   int src_swizzle_bit = 1;
   int next_free_swizzle_bit = 1;
   int k = 0;
   unsigned i;
   unsigned last_src_bit = util_last_bit(component_access_mask);

   for (i = 0; i <= last_src_bit ; ++i, src_swizzle_bit <<= 1) {

      /* Jump over empty src component slots (e.g. x__w). This is just a
       * safety measure and it is tested for, but it is very likely that the
       * emitted code always uses slots staring from x without leaving holes
       * (i.e. always xy__ not x_z_ or _yz_ etc).
       */
      if (!(src_swizzle_bit & component_access_mask))
	 continue;

      /* Find the next free access slot in the target. */
      while ((trgt_access_mask & next_free_swizzle_bit) &&
	     k < 4) {
	 next_free_swizzle_bit <<= 1;
	 ++k;
      }
      assert(k < 4 &&
	     "Interleaved array would have more then four components");

      /* Set the mapping for this component. */
      swizzle_map[i] = k;
      trgt_access_mask |= next_free_swizzle_bit;

      /* Update the joined access mask if we didn't just fill the mapping.*/
      if (src_swizzle_bit & component_access_mask)
	 summary_access_mask |= next_free_swizzle_bit;
   }

   other->set_access_mask(summary_access_mask);
   other->merge_live_range_from(this);

   ARRAY_MERGE_DUMP_BLOCK(
	    std::cerr << "Interleave " << id << " into " << other->id << ", swz:";
	    for (unsigned i = 0; i < 4; ++i) {
		std::cerr << ((swizzle_map[i] >= 0) ? xyzw[swizzle_map[i]] : '_');
	    }
	    std::cerr << '\n';
	    );
}

void array_live_range::merge_live_range_from(array_live_range *other)
{
   other->set_target(this);
   if (other->begin() < first_access)
      first_access = other->begin();
   if (other->end() > last_access)
      last_access = other->end();
}

int8_t array_live_range::remap_one_swizzle(int8_t idx) const
{
   // needs testing
   if (target_array) {
      idx = swizzle_map[idx];
      if (idx >=  0)
	 idx = target_array->remap_one_swizzle(idx);
   }
   return idx;
}

void array_live_range::set_target(array_live_range  *target)
{
   target_array = target;
}

void array_live_range::print(std::ostream& os) const
{
   os << "[id:" << id
      << ", length:" << length
      << ", (b:" << first_access
      << ", e:" << last_access
      << "), sw:" << (int)component_access_mask
      << ", nc:" << (int)used_component_count
      << "]";
}

bool array_live_range::time_doesnt_overlap(const array_live_range& other) const
{
   return (other.last_access < first_access ||
	   last_access < other.first_access);
}

namespace tgsi_array_merge {

array_remapping::array_remapping():
   target_id(0)
{
   for (int i = 0; i < 4; ++i) {
      read_swizzle_map[i] = i;
   }
}

array_remapping::array_remapping(int trgt_array_id, const int8_t swizzle[]):
   target_id(trgt_array_id)
{
   for (int i = 0; i < 4; ++i) {
      read_swizzle_map[i] = swizzle[i];
   }
}

void array_remapping::init_from(const array_live_range& range)
{
   target_id = range.is_mapped() ? range.final_target()->array_id(): 0;
   for (int i = 0; i < 4; ++i)
      read_swizzle_map[i] = range.remap_one_swizzle(i);
}


int array_remapping::map_writemask(int write_mask) const
{
   assert(is_valid());
   int result_write_mask = 0;
   for (int i = 0; i < 4; ++i) {
      if (1 << i & write_mask) {
	 assert(read_swizzle_map[i] >= 0);
	 result_write_mask |= 1 << read_swizzle_map[i];
      }
   }
   return result_write_mask;
}

uint16_t array_remapping::move_read_swizzles(uint16_t original_swizzle) const
{
   assert(is_valid());
   /* Since
    *
    *   dst.zw = src.xy in glsl actually is MOV dst.__zw src.__xy
    *
    * when interleaving the arrays the source swizzles must be moved
    * according to the changed dst write mask.
    */
   uint16_t out_swizzle = 0;
   for (int idx = 0; idx < 4; ++idx) {
      uint16_t orig_swz = GET_SWZ(original_swizzle, idx);
      int new_idx = read_swizzle_map[idx];
      if (new_idx >= 0)
	 out_swizzle |= orig_swz << 3 * new_idx;
   }
   return out_swizzle;
}

uint16_t array_remapping::map_swizzles(uint16_t old_swizzle) const
{
   uint16_t out_swizzle = 0;
   for (int idx = 0; idx < 4; ++idx) {
      uint16_t swz = read_swizzle_map[GET_SWZ(old_swizzle, idx)];
      out_swizzle |= swz << 3 * idx;
   }
   return out_swizzle;
}

void array_remapping::print(std::ostream& os) const
{
   if (is_valid()) {
      os << "[aid: " << target_id << " swz: ";
      for (int i = 0; i < 4; ++i)
	 os << (read_swizzle_map[i] >= 0 ? xyzw[read_swizzle_map[i]] : '_');
      os << "]";
   } else {
      os << "[unused]";
   }
}

/* Required by the unit tests */
bool operator == (const array_remapping& lhs, const array_remapping& rhs)
{
   if (lhs.target_id != rhs.target_id)
      return false;

   if (lhs.target_id == 0)
      return true;

   for (int i = 0; i < 4; ++i) {
      if (lhs.read_swizzle_map[i] != rhs.read_swizzle_map[i])
	 return false;
   }
   return true;
}

static
bool sort_by_begin(const array_live_range& lhs, const array_live_range& rhs) {
   return lhs.begin() < rhs.begin();
}

/* Helper class to evaluate merging and interleaving of arrays */
class array_merge_evaluator {
public:
   typedef int (*array_merger)(array_live_range& range_1,
			       array_live_range& range_2);

   array_merge_evaluator(int _narrays, array_live_range *_ranges,
			 bool _restart);

   /** Run the merge strategy on all arrays
    * @returns number of successfull merges
    */
   int run();

private:
   virtual int do_run(array_live_range& range_1, array_live_range& range_2) = 0;

   int narrays;
   array_live_range *ranges;
   bool restart;
};

array_merge_evaluator::array_merge_evaluator(int _narrays,
					     array_live_range *_ranges,
					     bool _restart):
   narrays(_narrays),
   ranges(_ranges),
   restart(_restart)
{
}

int array_merge_evaluator::run()
{
   int remaps = 0;

   for (int i = 0; i < narrays; ++i) {
      if (ranges[i].is_mapped())
	 continue;

      for (int j = i + 1; j < narrays; ++j) {
	 if (!ranges[j].is_mapped()) {
	    ARRAY_MERGE_DUMP("try merge " << i << " id:" << ranges[i].array_id()
			     << " and " << j  << " id: "<< ranges[j].array_id()
			     << "\n");
	    int n = do_run(ranges[i], ranges[j]);
	    if (restart && n)
	       return n;
	    remaps += n;
	 }
      }
   }
   return remaps;
}

/* Merge live ranges if possible at all */
class merge_live_range_always: public array_merge_evaluator {
public:
   merge_live_range_always(int _narrays, array_live_range *_ranges):
      array_merge_evaluator(_narrays, _ranges, false) {
   }
protected:
   int do_run(array_live_range& range_1, array_live_range& range_2){
      if (range_2.time_doesnt_overlap(range_1)) {
	 ARRAY_MERGE_DUMP("merge " << range_2 << " into " << range_1 << "\n");
	 array_live_range::merge(&range_1,&range_2);
	 return 1;
      }
      return 0;
   }
};

/* Merge live ranges only if they use the same swizzle */
class merge_live_range_equal_swizzle: public merge_live_range_always {
public:
   merge_live_range_equal_swizzle(int _narrays, array_live_range *_ranges):
      merge_live_range_always(_narrays, _ranges) {
   }
private:
   int do_run(array_live_range& range_1, array_live_range& range_2){
      if (range_1.access_mask() == range_2.access_mask()) {
	 return merge_live_range_always::do_run(range_1, range_2);
      }
      return 0;
   }
};

/* Interleave arrays if possible */
class interleave_live_range: public  array_merge_evaluator {
public:
   interleave_live_range(int _narrays, array_live_range *_ranges):
      array_merge_evaluator(_narrays, _ranges, true) {
   }
private:
   int do_run(array_live_range& range_1, array_live_range& range_2){
      if ((range_2.used_components() + range_1.used_components() <= 4) &&
	  !range_1.time_doesnt_overlap(range_2)) {
	 ARRAY_MERGE_DUMP("Interleave " << range_2 << " into " << range_1 << "\n");
	 array_live_range::interleave(&range_1, &range_2);
	 return 1;
      }
      return 0;
   }
};

/* Estimate the array merging: First in a loop, arrays with equal access mask
 * are merged, then interleave arrays that together use at most four components,
 * and have overlapping live ranges. Finally arrays are merged regardless of
 * access mask.
 * @param[in] narrays number of arrays
 * @param[in,out] alt array life times, the merge target life time will be
 *   updated with the new life time.
 * @param[in,out] remapping track the arraay index remapping and reswizzeling.
 * @returns number of merged arrays
 */
bool get_array_remapping(int narrays, array_live_range *ranges,
			 array_remapping *remapping)
{
   int total_remapped = 0;
   int n_remapped;

   /* Sort by "begin of live range" so that we don't have to restart searching
    * after every merge.
    */
   std::sort(ranges, ranges + narrays, sort_by_begin);
   merge_live_range_equal_swizzle merge_evaluator_es(narrays, ranges);
   interleave_live_range interleave_lr(narrays, ranges);
   do {

      n_remapped = merge_evaluator_es.run();

      /* try only one array interleave, if successfull, another
       * live_range merge is tried. The test MergeAndInterleave5
       * (mesa/st/tests/test_glsl_to_tgsi_array_merge.cpp)
       * shows that this can result in more arrays being merged/interleaved.
       */
      n_remapped += interleave_lr.run();
      total_remapped += n_remapped;

      ARRAY_MERGE_DUMP("Remapped " << n_remapped << " arrays\n");
   } while (n_remapped > 0);

   total_remapped += merge_live_range_always(narrays, ranges).run();
   ARRAY_MERGE_DUMP("Remapped a total of " << total_remapped << " arrays\n");

   /* Resolve the remapping chain */
   for (int i = 1; i <= narrays; ++i) {
      ARRAY_MERGE_DUMP("Map " << i << ":");
      remapping[ranges[i-1].array_id()].init_from(ranges[i-1]);
   }
   return total_remapped > 0;
}

/* Remap the arrays in a TGSI program according to the given mapping.
 * @param narrays number of arrays
 * @param array_sizes array of arrays sizes
 * @param map the array remapping information
 * @param instructions TGSI program
 * @returns number of arrays after remapping
 */
int remap_arrays(int narrays, unsigned *array_sizes,
		 exec_list *instructions,
		 array_remapping *map)
{
   /* re-calculate arrays */
#if __cplusplus < 201402L
   int *idx_map = new int[narrays + 1];
   unsigned *old_sizes = new unsigned[narrays];
#else
   unique_ptr<int[]> idx_map = make_unique<int[]>(narrays + 1);
   unique_ptr<unsigned[]> old_sizes = make_unique<unsigned[]>(narrays);
#endif

   memcpy(&old_sizes[0], &array_sizes[0], sizeof(unsigned) * narrays);

   /* Evaluate mapping for the array indices and update array sizes */
   int new_narrays = 0;
   for (int i = 1; i <= narrays; ++i) {
      if (!map[i].is_valid()) {
         ++new_narrays;
         array_sizes[new_narrays-1] = old_sizes[i-1];
         idx_map[i] = new_narrays;
      }
   }

   /* Map the array ids of merged arrays. */
   for (int i = 1; i <= narrays; ++i) {
      if (map[i].is_valid()) {
	 map[i].set_target_id(idx_map[map[i].target_array_id()]);
      }
   }

   /* Map the array ids of merge targets that got only renumbered. */
   for (int i = 1; i <= narrays; ++i) {
      if (!map[i].is_valid()) {
	 map[i].set_target_id(idx_map[i]);
      }
   }

   /* Update the array ids and swizzles in the registers */
   foreach_in_list(glsl_to_tgsi_instruction, inst, instructions) {
      for (unsigned j = 0; j < num_inst_src_regs(inst); j++) {
	 st_src_reg& src = inst->src[j];
	 if (src.file == PROGRAM_ARRAY && src.array_id > 0) {
	    array_remapping& m = map[src.array_id];
	    if (m.is_valid()) {
	       src.array_id = m.target_array_id();
	       src.swizzle = m.map_swizzles(src.swizzle);
	    }
	 }
      }
      for (unsigned j = 0; j < inst->tex_offset_num_offset; j++) {
	 st_src_reg& src = inst->tex_offsets[j];
	 if (src.file == PROGRAM_ARRAY && src.array_id > 0) {
	    array_remapping& m = map[src.array_id];
	    if (m.is_valid()) {
	       src.array_id = m.target_array_id();
	       src.swizzle = m.map_swizzles(src.swizzle);
	    }
	 }
      }
      for (unsigned j = 0; j < num_inst_dst_regs(inst); j++) {
	 st_dst_reg& dst = inst->dst[j];
	 if (dst.file == PROGRAM_ARRAY && dst.array_id > 0) {
	    array_remapping& m = map[dst.array_id];
	    if (m.is_valid()) {
	       assert(j == 0 &&
		      "remapping can only be done for single dest ops");
	       dst.array_id = m.target_array_id();
	       dst.writemask = m.map_writemask(dst.writemask);

	       /* If the target component is moved, then the source swizzles
		* must be moved accordingly.
		*/
	       for (unsigned j = 0; j < num_inst_src_regs(inst); j++) {
		  st_src_reg& src = inst->src[j];
		  src.swizzle = m.move_read_swizzles(src.swizzle);
	       }
	    }
	 }
      }
      st_src_reg& res = inst->resource;
      if (res.file == PROGRAM_ARRAY && res.array_id > 0) {
	 array_remapping& m = map[res.array_id];
	 if (m.is_valid()) {
	    res.array_id = m.target_array_id();
	    res.swizzle = m.map_swizzles(res.swizzle);
	 }
      }
   }

#if __cplusplus < 201402L
   delete[] old_sizes;
   delete[] idx_map;
#endif

   return new_narrays;
}

}

using namespace tgsi_array_merge;

int  merge_arrays(int narrays,
		  unsigned *array_sizes,
		  exec_list *instructions,
		  class array_live_range *arr_live_ranges)
{
   array_remapping *map= new array_remapping[narrays + 1];

   if (get_array_remapping(narrays, arr_live_ranges, map))
      narrays = remap_arrays(narrays, array_sizes, instructions, map);

   delete[] map;
   return narrays;
}