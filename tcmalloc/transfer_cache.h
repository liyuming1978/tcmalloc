// Copyright 2019 The TCMalloc Authors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef TCMALLOC_TRANSFER_CACHE_H_
#define TCMALLOC_TRANSFER_CACHE_H_

#include <stddef.h>
#include <stdint.h>

#include <atomic>
#include <utility>

#include "absl/base/const_init.h"
#include "absl/base/internal/spinlock.h"
#include "absl/base/macros.h"
#include "absl/base/thread_annotations.h"
#include "absl/types/span.h"
#include "tcmalloc/central_freelist.h"
#include "tcmalloc/common.h"

#ifndef TCMALLOC_SMALL_BUT_SLOW
#include "tcmalloc/transfer_cache_internals.h"
#endif

namespace tcmalloc {

#ifndef TCMALLOC_SMALL_BUT_SLOW

class TransferCacheManager {
  template <typename CentralFreeList, typename Manager>
  friend class internal_transfer_cache::TransferCache;
  using TransferCache =
      internal_transfer_cache::TransferCache<CentralFreeList,
                                             TransferCacheManager>;

  template <size_t... Idx>
  constexpr explicit TransferCacheManager(std::index_sequence<Idx...> i)
      : cache_{{this, Idx}...}, next_to_evict_(1) {
    static_assert(sizeof...(Idx) == kNumClasses);
  }

 public:
  constexpr TransferCacheManager()
      : TransferCacheManager(std::make_index_sequence<kNumClasses>{}) {}

  TransferCacheManager(const TransferCacheManager &) = delete;
  TransferCacheManager &operator=(const TransferCacheManager &) = delete;

  void Init() ABSL_EXCLUSIVE_LOCKS_REQUIRED(pageheap_lock) {
    for (int i = 0; i < kNumClasses; ++i) {
      cache_[i].Init(i);
    }
  }

  void InsertRange(int size_class, absl::Span<void *> batch, int n) {
    cache_[size_class].InsertRange(batch, n);
  }

  int RemoveRange(int size_class, void **batch, int n) {
    return cache_[size_class].RemoveRange(batch, n);
  }

  size_t central_length(int size_class) {
    return cache_[size_class].central_length();
  }

  size_t tc_length(int size_class) { return cache_[size_class].tc_length(); }

  size_t OverheadBytes(int size_class) {
    return cache_[size_class].OverheadBytes();
  }

  SpanStats GetSpanStats(int size_class) const {
    return cache_[size_class].GetSpanStats();
  }

 private:
  static size_t class_to_size(int size_class);
  static size_t num_objects_to_move(int size_class);
  void *Alloc(size_t size) EXCLUSIVE_LOCKS_REQUIRED(pageheap_lock);
  int DetermineSizeClassToEvict();
  bool ShrinkCache(int size_class) { return cache_[size_class].ShrinkCache(); }

  TransferCache cache_[kNumClasses];
  std::atomic<int32_t> next_to_evict_;
} ABSL_CACHELINE_ALIGNED;

#else

// For the small memory model, the transfer cache is not used.
class TransferCacheManager {
 public:
  constexpr TransferCacheManager() : freelist_() {}
  TransferCacheManager(const TransferCacheManager &) = delete;
  TransferCacheManager &operator=(const TransferCacheManager &) = delete;

  void Init() EXCLUSIVE_LOCKS_REQUIRED(pageheap_lock) {
    for (int i = 0; i < kNumClasses; ++i) {
      freelist_[i].Init(i);
    }
  }

  void InsertRange(int size_class, absl::Span<void *> batch, int n) {
    freelist_[size_class].InsertRange(batch.data(), n);
  }

  int RemoveRange(int size_class, void **batch, int n) {
    return freelist_[size_class].RemoveRange(batch, n);
  }

  size_t central_length(int size_class) {
    return freelist_[size_class].length();
  }

  size_t tc_length(int size_class) { return 0; }

  size_t OverheadBytes(int size_class) {
    return freelist_[size_class].OverheadBytes();
  }

  SpanStats GetSpanStats(int size_class) const {
    return freelist_[size_class].GetSpanStats();
  }

 private:
  CentralFreeList freelist_[kNumClasses];
} ABSL_CACHELINE_ALIGNED;

#endif
}  // namespace tcmalloc

#endif  // TCMALLOC_TRANSFER_CACHE_H_
