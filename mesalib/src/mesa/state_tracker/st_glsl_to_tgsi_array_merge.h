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

#ifndef MESA_GLSL_TO_TGSI_ARRAY_MERGE_H
#define MESA_GLSL_TO_TGSI_ARRAY_MERGE_H


#include "st_glsl_to_tgsi_private.h"
#include <iosfwd>

/* Until mesa/st officialy requires c++11 */
#if __cplusplus < 201103L
#define nullptr 0
#endif

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

inline
std::ostream& operator << (std::ostream& os, const array_live_range& lt) {
   lt.print(os);
   return os;
}

namespace tgsi_array_merge {

/* Helper class to apply array merge and interleav to the shader.
 * The interface is exposed here to make unit tests possible.
 */
class array_remapping {
public:

   /** Create an invalid mapping that is used as place-holder for
    * arrays that are not mapped at all.
    */
   array_remapping();

   /* Predefined remapping, needed for testing */
   array_remapping(int trgt_array_id, const int8_t swizzle[]);

   /* Initialiaze the mapping from an array_live_range that has been
    * processed by the array merge and interleave algorithm.
    */
   void init_from(const array_live_range& range);

   /* (Re)-set target id, needed when the mapping is resolved */
   void set_target_id(int tid) {target_id = tid;}

   /* Defines a valid remapping */
   bool is_valid() const {return target_id > 0;}

   /* Translates the write mask to the new, interleaved component
    * position
    */
   int map_writemask(int original_write_mask) const;

   /* Translates all read swizzles to the new, interleaved component
    * swizzles
    */
   uint16_t map_swizzles(uint16_t original_swizzle) const;

   /* Move the read swizzles to the positiones that correspond to
    * a changed write mask.
    */
   uint16_t move_read_swizzles(uint16_t original_swizzle) const;

   unsigned target_array_id() const {return target_id;}

   void print(std::ostream& os) const;

   friend bool operator == (const array_remapping& lhs,
			    const array_remapping& rhs);

private:

   void interleave(int trgt_access_mask, int src_access_mask);

   unsigned target_id;
   int8_t read_swizzle_map[4];
};

inline
std::ostream& operator << (std::ostream& os, const array_remapping& am)
{
   am.print(os);
   return os;
}

/* Apply the array remapping (internal use, exposed here for testing) */
 bool get_array_remapping(int narrays, array_live_range *array_live_ranges,
			 array_remapping *remapping);

/* Apply the array remapping (internal use, exposed here for testing) */
int remap_arrays(int narrays, unsigned *array_sizes,
		 exec_list *instructions,
		 array_remapping *map);

}

/** Remap the array access to finalize the array merging and interleaving.
  * @param[in] narrays number of input arrays,
  * @param[in,out] array_sizes length array of input arrays, on output the
  *   array sizes will be updated according to the remapping,
  * @param[in,out] instructions TGSI program, on output the arrays access is
  *    remapped to the new array layout,
  * @param[in] array_live_ranges live ranges and access information of the
  *    arrays.
  * @returns number of remaining arrays
  */
int merge_arrays(int narrays,
		 unsigned *array_sizes,
		 exec_list *instructions,
		 class array_live_range *arr_live_ranges);
#endif
