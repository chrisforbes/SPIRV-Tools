// Copyright (c) 2017 Google Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef LIBSPIRV_SMALL_VECTOR_H_
#define LIBSPIRV_SMALL_VECTOR_H_

#include <algorithm>
#include <utility>
#include <type_traits>
#include <memory>

// A _simple_ small_vector. Up to N elements are stored inline.
// Only implements a subset of the std::vector interface.
// Doesn't particularly care about exceptions (we have to work
// in environments without them).

template<typename T, unsigned N>
class small_vector
{
public:
  using value_type = T;
  using size_type = size_t;
  using difference_type = ptrdiff_t;

  using storage_type = typename std::aligned_storage<sizeof(T), alignof(T)>::type;
  using iterator = T *;
  using const_iterator = T const *;
  using reference = value_type&;
  using const_reference = value_type const &;

private:
  size_type size_, capacity_;
  union {
    storage_type small_data_[N];
    std::unique_ptr<storage_type[]> data_;
  };

  void call_dtors(iterator a, iterator b) {
    for (; a < b; a++)
      a->~T();
  }

  void move_construct_elems(iterator in1, iterator in2, iterator out) {
    while (in1 < in2)
      new (out++) value_type(std::move(*in1++));
  }

  void copy_construct_elems(const_iterator in1, const_iterator in2, iterator out) {
    while (in1 < in2)
      new (out++) value_type(*in1++);
  }

public:
  small_vector()
    : size_(0), capacity_(N)
  {}

  small_vector(const_iterator first, const_iterator last)
    : size_(0), capacity_(N)
  {
    reserve(last - first);
    copy_construct_elems(first, last, begin());
    size_ = last - first;
  }

  small_vector(std::initializer_list<value_type> init)
    : size_(0), capacity_(N)
  {
    reserve(init.size());
    copy_construct_elems(init.begin(), init.end(), begin());
    size_ = init.size();
  }

  small_vector(small_vector<T, N> const & v)
    : size_(0), capacity_(N) {
    reserve(v.size());
    copy_construct_elems(v.begin(), v.end(), begin());
    size_ = v.size();
  }
 
  small_vector(small_vector<T, N> && v)
    : size_(v.size_), capacity_(v.capacity_) {
    if (v.capacity_ == v.size_) {
      // move in internal mode
      move_construct_elems(v.begin(), v.end(), begin());
      call_dtors(v.begin(), v.end());
      v.size_ = 0;
    }
    else {
      // move in external mode (steal the guts)
      data_ = std::move(v.data_);
      // donor reverts to empty, internal mode
      v.capacity_ = N;
      v.size_ = 0;
    }
  }

  ~small_vector() {
    call_dtors(begin(), end());
    if (capacity_ != N)
      data_.~unique_ptr();
  }

  void reserve(size_type new_capacity) {
    if (new_capacity <= capacity_)
      return;
    std::unique_ptr<storage_type[]> new_data(new storage_type[new_capacity]);
    move_construct_elems(begin(), end(), reinterpret_cast<iterator>(&new_data[0]));
    call_dtors(begin(), end());
    if (capacity_ == N) // switching internal->external
      new (&data_) std::unique_ptr<storage_type[]>();
    data_ = std::move(new_data);
    capacity_ = new_capacity;
  }

  void shrink_to_fit() {
    if (capacity_ == N)
      return; // do nothing, we're already on internal storage.
    auto it1 = begin();
    auto it2 = end();
    std::unique_ptr<storage_type[]> old_data(std::move(data_));
    capacity_ = std::max(size_type(N), size_);
    if (capacity_ != N)
      data_.reset(new storage_type[capacity_]);
    move_construct_elems(it1, it2, begin());
    call_dtors(it1, it2);
  }

  void push_back(const_reference t) {
    if (size_ == capacity_) reserve(capacity_ * 2);
    new (end()) value_type(t);
    ++size_;
  }

  template<typename... Args>
  void emplace_back(Args&&... args) {
    if (size_ == capacity_) reserve(capacity_ * 2);
    new (end()) value_type(std::forward<Args>(args)...);
    ++size_;
  }

  void pop_back() {
    --size_;
    end()->~T();
  }

  void clear() {
    call_dtors(begin(), end());
    size_ = 0;
  }

  const_iterator begin() const {
    return (capacity_ == N)
      ? reinterpret_cast<const_iterator>(&small_data_[0])
      : reinterpret_cast<const_iterator>(&data_[0]);
  }

  const_iterator end() const {
    return begin() + size_;
  }

  iterator begin() {
    return (capacity_ == N)
      ? reinterpret_cast<iterator>(&small_data_[0])
      : reinterpret_cast<iterator>(&data_[0]);
  }

  iterator end() {
    return begin() + size_;
  }

  iterator data() { return begin() };
  const_iterator data() const { return begin(); }

  size_type size() const { return size_; }
  size_type capacity() const { return capacity_; }

  const_reference front() const { return *begin(); }
  const_reference back() const { return *(begin() + size_ - 1); }

  reference front() { return *begin(); }
  reference back() { return *(begin() + size_ - 1); }

  reference operator[](size_t index) { return *(begin() + index); }
  const_reference operator[](size_t index) const { return *(begin() + index); }
};

// non-owning view onto some underlying contiguous sequence of Ts
template<typename T>
class span
{
  T* data_;
  size_t len_;

public:
  using iterator = T*;
  using pointer = T*;
  using reference = T&;
  using const_iterator = T const *;
  using const_pointer = T const *;
  using const_reference = T const &;

  span() : data_{nullptr}, len_{0} {}
  span(T* data, size_t len) : data_{ data }, len_{ len } {}
  span(T const & t) : data_{ t.data_ }, len_{ t.len_ } {}

  iterator begin() { return data_; }
  iterator end() { return data_ + len_; }
  const_iterator begin() const { return data_; }
  const_iterator end() const { return data_ + len_; }
  reference front() { return data_[0]; }
  reference back() { return data_[len_ - 1]; }
  const_reference front() const { return data_[0]; }
  const_reference back() const { return data_[len_ - 1]; }
  size_t size() const { return len_; }
  pointer data() { return data_; }
  const_pointer data() const { return data_; }

  reference operator[](size_t n) { return data_[n]; }
  const_reference operator[](size_t n) const { return data_[n]; }
};

#endif  // LIBSPIRV_SMALL_VECTOR_H_