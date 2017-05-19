/*
 * Copyright (C) 2016 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#ifndef GCE_THUNKERS
#define GCE_THUNKERS

template <typename HalType, typename Impl, typename F> struct ThunkerBase;

/* Handle varying number of arguments with a bunch of specializations.
 * The C++11 dream is:
 *
 * template <typename HalType, typename Impl, typename R, typename... Args>
 * struct ThunkerBase<HalType, Impl, R(Args...)> {
 *   template <R (Impl::*MemFn)(Args...)>
 *   static R call(HalType* in, Args... args) {
 *     return (reinterpret_cast<Impl*>(in)->*MemFn)(args...);
 *   }
 * };
 */

template <typename HalType, typename Impl, typename R>
struct ThunkerBase<HalType, Impl, R()> {
  template <R (Impl::*MemFn)()>
  static R call(HalType* in) {
    return (reinterpret_cast<Impl*>(in)->*MemFn)();
  }

  template <R (Impl::*MemFn)() const>
  static R call(const HalType* in) {
    return (reinterpret_cast<const Impl*>(in)->*MemFn)();
  }
};

template <typename HalType, typename Impl, typename R, typename T1>
struct ThunkerBase<HalType, Impl, R(T1)> {
  template <R (Impl::*MemFn)(T1)>
  static R call(HalType* in, T1 t1) {
    return (reinterpret_cast<Impl*>(in)->*MemFn)(t1);
  }

  template <R (Impl::*MemFn)(T1) const>
  static R call(const HalType* in, T1 t1) {
    return (reinterpret_cast<const Impl*>(in)->*MemFn)(t1);
  }
};

template <typename HalType, typename Impl, typename R, typename T1, typename T2>
struct ThunkerBase<HalType, Impl, R(T1, T2)> {
  template <R (Impl::*MemFn)(T1, T2)>
  static R call(HalType* in, T1 t1, T2 t2) {
    return (reinterpret_cast<Impl*>(in)->*MemFn)(t1, t2);
  }

  template <R (Impl::*MemFn)(T1, T2) const>
  static R call(const HalType* in, T1 t1, T2 t2) {
    return (reinterpret_cast<const Impl*>(in)->*MemFn)(t1, t2);
  }
};

template <typename HalType, typename Impl, typename R, typename T1,
          typename T2, typename T3>
struct ThunkerBase<HalType, Impl, R(T1, T2, T3)> {
  template <R (Impl::*MemFn)(T1, T2, T3)>
  static R call(HalType* in, T1 t1, T2 t2, T3 t3) {
    return (reinterpret_cast<Impl*>(in)->*MemFn)(t1, t2, t3);
  }

  template <R (Impl::*MemFn)(T1, T2, T3) const>
  static R call(const HalType* in, T1 t1, T2 t2, T3 t3) {
    return (reinterpret_cast<const Impl*>(in)->*MemFn)(t1, t2, t3);
  }
};

template <typename HalType, typename Impl, typename R, typename T1,
          typename T2, typename T3, typename T4>
struct ThunkerBase<HalType, Impl, R(T1, T2, T3, T4)> {
  template <R (Impl::*MemFn)(T1, T2, T3, T4)>
  static R call(HalType* in, T1 t1, T2 t2, T3 t3, T4 t4) {
    return (reinterpret_cast<Impl*>(in)->*MemFn)(t1, t2, t3, t4);
  }

  template <R (Impl::*MemFn)(T1, T2, T3, T4) const>
  static R call(const HalType* in, T1 t1, T2 t2, T3 t3, T4 t4) {
    return (reinterpret_cast<const Impl*>(in)->*MemFn)(t1, t2, t3, t4);
  }
};

template <typename HalType, typename Impl, typename R, typename T1,
          typename T2, typename T3, typename T4, typename T5>
struct ThunkerBase<HalType, Impl, R(T1, T2, T3, T4, T5)> {
  template <R (Impl::*MemFn)(T1, T2, T3, T4, T5)>
  static R call(HalType* in, T1 t1, T2 t2, T3 t3, T4 t4, T5 t5) {
    return (reinterpret_cast<Impl*>(in)->*MemFn)(t1, t2, t3, t4, t5);
  }

  template <R (Impl::*MemFn)(T1, T2, T3, T4, T5) const>
  static R call(const HalType* in, T1 t1, T2 t2, T3 t3, T4 t4, T5 t5) {
    return (reinterpret_cast<const Impl*>(in)->*MemFn)(t1, t2, t3, t4, t5);
  }
};

template <typename HalType, typename Impl, typename R, typename T1,
          typename T2, typename T3, typename T4, typename T5, typename T6>
struct ThunkerBase<HalType, Impl, R(T1, T2, T3, T4, T5, T6)> {
  template <R (Impl::*MemFn)(T1, T2, T3, T4, T5, T6)>
  static R call(HalType* in, T1 t1, T2 t2, T3 t3, T4 t4, T5 t5, T6 t6) {
    return (reinterpret_cast<Impl*>(in)->*MemFn)(t1, t2, t3, t4, t5, t6);
  }

  template <R (Impl::*MemFn)(T1, T2, T3, T4, T5, T6) const>
  static R call(const HalType* in, T1 t1, T2 t2, T3 t3, T4 t4, T5 t5, T6 t6) {
    return (reinterpret_cast<const Impl*>(in)->*MemFn)(t1, t2, t3, t4, t5, t6);
  }
};

template <typename HalType, typename Impl, typename R, typename T1,
          typename T2, typename T3, typename T4, typename T5, typename T6,
          typename T7>
struct ThunkerBase<HalType, Impl, R(T1, T2, T3, T4, T5, T6, T7)> {
  template <R (Impl::*MemFn)(T1, T2, T3, T4, T5, T6, T7)>
  static R call(HalType* in, T1 t1, T2 t2, T3 t3, T4 t4, T5 t5, T6 t6, T7 t7) {
    return (reinterpret_cast<Impl*>(in)->*MemFn)(t1, t2, t3, t4, t5, t6, t7);
  }

  template <R (Impl::*MemFn)(T1, T2, T3, T4, T5, T6, T7) const>
  static R call(const HalType* in, T1 t1, T2 t2, T3 t3, T4 t4, T5 t5, T6 t6,
                T7 t7) {
    return (reinterpret_cast<const Impl*>(in)->*MemFn)(
        t1, t2, t3, t4, t5, t6, t7);
  }
};

template <typename HalType, typename Impl, typename R, typename T1,
          typename T2, typename T3, typename T4, typename T5, typename T6,
          typename T7, typename T8>
struct ThunkerBase<HalType, Impl, R(T1, T2, T3, T4, T5, T6, T7, T8)> {
  template <R (Impl::*MemFn)(T1, T2, T3, T4, T5, T6, T7, T8)>
  static R call(HalType* in, T1 t1, T2 t2, T3 t3, T4 t4, T5 t5, T6 t6, T7 t7,
                T8 t8) {
    return (reinterpret_cast<Impl*>(in)->*MemFn)(
        t1, t2, t3, t4, t5, t6, t7, t8);
  }

  template <R (Impl::*MemFn)(T1, T2, T3, T4, T5, T6, T7, T8) const>
  static R call(const HalType* in, T1 t1, T2 t2, T3 t3, T4 t4, T5 t5, T6 t6,
                T7 t7, T8 t8) {
    return (reinterpret_cast<const Impl*>(in)->*MemFn)(
        t1, t2, t3, t4, t5, t6, t7, t8);
  }
};
#endif  // GCE_THUNKERS
