/*
 * Copyright Michael Schellenberger Costa
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
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 *
 */

#ifndef ACO_UTIL_H
#define ACO_UTIL_H

#include <cassert>
#include <iterator>

namespace aco {

/*! \brief      Definition of a span object
*
*   \details    A "span" is an "array view" type for holding a view of contiguous
*               data. The "span" object does not own the data itself.
*/
template <typename T>
class span {
public:
   using value_type             = T;
   using pointer                = value_type*;
   using const_pointer          = const value_type*;
   using reference              = value_type&;
   using const_reference        = const value_type&;
   using iterator               = pointer;
   using const_iterator         = const_pointer;
   using reverse_iterator       = std::reverse_iterator<iterator>;
   using const_reverse_iterator = std::reverse_iterator<const_iterator>;
   using size_type              = std::size_t;
   using difference_type        = std::ptrdiff_t;

   /*! \brief                  Compiler generated default constructor
   */
   constexpr span() = default;

   /*! \brief                 Constructor taking a pointer and the length of the span
   *   \param[in]   data      Pointer to the underlying data array
   *   \param[in]   length    The size of the span
   */
   constexpr span(pointer data, const size_type length)
       : data{ data } , length{ length } {}

   /*! \brief                 Returns an iterator to the begin of the span
   *   \return                data
   */
   constexpr iterator begin() noexcept {
      return data;
   }

   /*! \brief                 Returns a const_iterator to the begin of the span
   *   \return                data
   */
   constexpr const_iterator begin() const noexcept {
      return data;
   }

   /*! \brief                 Returns an iterator to the end of the span
   *   \return                data + length
   */
   constexpr iterator end() noexcept {
      return std::next(data, length);
   }

   /*! \brief                 Returns a const_iterator to the end of the span
   *   \return                data + length
   */
   constexpr const_iterator end() const noexcept {
      return std::next(data, length);
   }

   /*! \brief                 Returns a const_iterator to the begin of the span
   *   \return                data
   */
   constexpr const_iterator cbegin() const noexcept {
      return data;
   }

   /*! \brief                 Returns a const_iterator to the end of the span
   *   \return                data + length
   */
   constexpr const_iterator cend() const noexcept {
      return std::next(data, length);
   }

   /*! \brief                 Returns a reverse_iterator to the end of the span
   *   \return                reverse_iterator(end())
   */
   constexpr reverse_iterator rbegin() noexcept {
      return reverse_iterator(end());
   }

   /*! \brief                 Returns a const_reverse_iterator to the end of the span
   *   \return                reverse_iterator(end())
   */
   constexpr const_reverse_iterator rbegin() const noexcept {
      return const_reverse_iterator(end());
   }

   /*! \brief                 Returns a reverse_iterator to the begin of the span
   *   \return                reverse_iterator(begin())
   */
   constexpr reverse_iterator rend() noexcept {
      return reverse_iterator(begin());
   }

   /*! \brief                 Returns a const_reverse_iterator to the begin of the span
   *   \return                reverse_iterator(begin())
   */
   constexpr const_reverse_iterator rend() const noexcept {
      return const_reverse_iterator(begin());
   }

   /*! \brief                 Returns a const_reverse_iterator to the end of the span
   *   \return                rbegin()
   */
   constexpr const_reverse_iterator crbegin() const noexcept {
      return const_reverse_iterator(cend());
   }

   /*! \brief                 Returns a const_reverse_iterator to the begin of the span
   *   \return                rend()
   */
   constexpr const_reverse_iterator crend() const noexcept {
      return const_reverse_iterator(cbegin());
   }

   /*! \brief                 Unchecked access operator
   *   \param[in] index       Index of the element we want to access
   *   \return                *(std::next(data, index))
   */
   constexpr reference operator[](const size_type index) noexcept {
      assert(length > index);
      return *(std::next(data, index));
   }

   /*! \brief                 Unchecked const access operator
   *   \param[in] index       Index of the element we want to access
   *   \return                *(std::next(data, index))
   */
   constexpr const_reference operator[](const size_type index) const noexcept {
      assert(length > index);
      return *(std::next(data, index));
   }

   /*! \brief                 Returns a reference to the last element of the span
   *   \return                *(std::next(data, length - 1))
   */
   constexpr reference back() noexcept {
      assert(length > 0);
      return *(std::next(data, length - 1));
   }

   /*! \brief                 Returns a const_reference to the last element of the span
   *   \return                *(std::next(data, length - 1))
   */
   constexpr const_reference back() const noexcept {
      assert(length > 0);
      return *(std::next(data, length - 1));
   }

   /*! \brief                 Returns a reference to the first element of the span
   *   \return                *begin()
   */
   constexpr reference front() noexcept {
      assert(length > 0);
      return *begin();
   }

   /*! \brief                 Returns a const_reference to the first element of the span
   *   \return                *cbegin()
   */
   constexpr const_reference front() const noexcept {
      assert(length > 0);
      return *cbegin();
   }

   /*! \brief                 Returns true if the span is empty
   *   \return                length == 0
   */
   constexpr bool empty() const noexcept {
      return length == 0;
   }

   /*! \brief                 Returns the size of the span
   *   \return                length == 0
   */
   constexpr size_type size() const noexcept {
      return length;
   }

   /*! \brief                 Decreases the size of the span by 1
   */
   constexpr void pop_back() noexcept {
      assert(length > 0);
      --length;
   }

   /*! \brief                 Clears the span
   */
   constexpr void clear() noexcept {
      data = nullptr;
      length = 0;
   }

private:
   pointer data{ nullptr };   //!> Pointer to the underlying data array
   size_type length{ 0 };     //!> Size of the span
};

} // namespace aco

#endif // ACO_UTIL_H