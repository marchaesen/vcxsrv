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
#include <vector>

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
   using size_type              = uint16_t;
   using difference_type        = ptrdiff_t;

   /*! \brief                  Compiler generated default constructor
   */
   constexpr span() = default;

   /*! \brief                 Constructor taking a pointer and the length of the span
   *   \param[in]   data      Pointer to the underlying data array
   *   \param[in]   length    The size of the span
   */
   constexpr span(uint16_t offset, const size_type length)
       : offset{ offset } , length{ length } {}

   /*! \brief                 Returns an iterator to the begin of the span
   *   \return                data
   */
   constexpr iterator begin() noexcept {
      return (pointer)((uintptr_t)this + offset);
   }

   /*! \brief                 Returns a const_iterator to the begin of the span
   *   \return                data
   */
   constexpr const_iterator begin() const noexcept {
      return (const_pointer)((uintptr_t)this + offset);
   }

   /*! \brief                 Returns an iterator to the end of the span
   *   \return                data + length
   */
   constexpr iterator end() noexcept {
      return std::next(begin(), length);
   }

   /*! \brief                 Returns a const_iterator to the end of the span
   *   \return                data + length
   */
   constexpr const_iterator end() const noexcept {
      return std::next(begin(), length);
   }

   /*! \brief                 Returns a const_iterator to the begin of the span
   *   \return                data
   */
   constexpr const_iterator cbegin() const noexcept {
      return begin();
   }

   /*! \brief                 Returns a const_iterator to the end of the span
   *   \return                data + length
   */
   constexpr const_iterator cend() const noexcept {
      return std::next(begin(), length);
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
      return *(std::next(begin(), index));
   }

   /*! \brief                 Unchecked const access operator
   *   \param[in] index       Index of the element we want to access
   *   \return                *(std::next(data, index))
   */
   constexpr const_reference operator[](const size_type index) const noexcept {
      assert(length > index);
      return *(std::next(begin(), index));
   }

   /*! \brief                 Returns a reference to the last element of the span
   *   \return                *(std::next(data, length - 1))
   */
   constexpr reference back() noexcept {
      assert(length > 0);
      return *(std::next(begin(), length - 1));
   }

   /*! \brief                 Returns a const_reference to the last element of the span
   *   \return                *(std::next(data, length - 1))
   */
   constexpr const_reference back() const noexcept {
      assert(length > 0);
      return *(std::next(begin(), length - 1));
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

   /*! \brief                 Adds an element to the end of the span
   */
   constexpr void push_back(const_reference val) noexcept {
      *std::next(begin(), length++) = val;
   }

   /*! \brief                 Clears the span
   */
   constexpr void clear() noexcept {
      offset = 0;
      length = 0;
   }

private:
   uint16_t offset{ 0 };      //!> Byte offset from span to data
   size_type length{ 0 };     //!> Size of the span
};

/*
 * Cache-friendly set of 32-bit IDs with O(1) insert/erase/lookup and
 * the ability to efficiently iterate over contained elements.
 *
 * Internally implemented as a bit vector: If the set contains an ID, the
 * corresponding bit is set. It doesn't use std::vector<bool> since we then
 * couldn't efficiently iterate over the elements.
 *
 * The interface resembles a subset of std::set/std::unordered_set.
 */
struct IDSet {
   struct Iterator {
      const IDSet *set;
      union {
         struct {
            uint32_t bit:6;
            uint32_t word:26;
         };
         uint32_t id;
      };

      Iterator& operator ++();

      bool operator != (const Iterator& other) const;

      uint32_t operator * () const;
   };

   size_t count(uint32_t id) const {
      if (id >= words.size() * 64)
         return 0;

      return words[id / 64u] & (1ull << (id % 64u)) ? 1 : 0;
   }

   Iterator find(uint32_t id) const {
      if (!count(id))
         return end();

      Iterator it;
      it.set = this;
      it.bit = id % 64u;
      it.word = id / 64u;
      return it;
   }

   std::pair<Iterator, bool> insert(uint32_t id) {
      if (words.size() * 64u <= id)
         words.resize(DIV_ROUND_UP(id + 1, 64u));

      Iterator it;
      it.set = this;
      it.bit = id % 64u;
      it.word = id / 64u;

      uint64_t mask = 1ull << it.bit;
      if (words[it.word] & mask)
         return std::make_pair(it, false);

      words[it.word] |= mask;
      bits_set++;
      return std::make_pair(it, true);
   }

   size_t erase(uint32_t id) {
      if (!count(id))
         return 0;

      words[id / 64u] ^= 1ull << (id % 64u);
      bits_set--;
      return 1;
   }

   Iterator cbegin() const {
      Iterator it;
      it.set = this;
      for (size_t i = 0; i < words.size(); i++) {
         if (words[i]) {
            it.word = i;
            it.bit = ffsll(words[i]) - 1;
            return it;
         }
      }
      return end();
   }

   Iterator cend() const {
      Iterator it;
      it.set = this;
      it.word = words.size();
      it.bit = 0;
      return it;
   }

   Iterator begin() const {
      return cbegin();
   }

   Iterator end() const {
      return cend();
   }

   bool empty() const {
      return bits_set == 0;
   }

   size_t size() const {
      return bits_set;
   }

   std::vector<uint64_t> words;
   uint32_t bits_set;
};

inline IDSet::Iterator& IDSet::Iterator::operator ++() {
   uint64_t m = set->words[word];
   m &= ~((2ull << bit) - 1ull);
   if (!m) {
      /* don't continue past the end */
      if (word == set->words.size())
         return *this;

      word++;
      for (; word < set->words.size(); word++) {
         if (set->words[word]) {
            bit = ffsll(set->words[word]) - 1;
            return *this;
         }
      }
      bit = 0;
   } else {
      bit = ffsll(m) - 1;
   }
   return *this;
}

inline bool IDSet::Iterator::operator != (const IDSet::Iterator& other) const {
   assert(set == other.set);
   return id != other.id;
}

inline uint32_t IDSet::Iterator::operator * () const {
   return (word << 6) | bit;
}

} // namespace aco

#endif // ACO_UTIL_H
