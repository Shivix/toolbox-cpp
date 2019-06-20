// The Reactive C++ Toolbox.
// Copyright (C) 2019 Reactive Markets Limited
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

#ifndef TOOLBOX_UTIL_BENCHMARK_HPP
#define TOOLBOX_UTIL_BENCHMARK_HPP

#include <toolbox/Config.h>

#include "Traits.hpp"

#include <atomic>
#include <chrono>
#include <iostream>
#include <map>
#include <thread>

#include <unistd.h>

namespace toolbox {
inline namespace benchmark {

// The clobber and use functions below are used to defeat the optimiser.
// See CppCon 2015: Chandler Carruth "Tuning C++: Benchmarks, and CPUs, and Compilers! Oh My!"
// And https://github.com/google/benchmark/blob/master/include/benchmark/benchmark.h

/// The clobber function instructs the compiler that something might have changed in the memory and
/// no assumptions can be made that would discard memory writes.
inline void clobber() noexcept
{
    asm volatile("" ::: "memory");
}

/// The use function emulates the creation additional uses of the reference essentially instructing
/// the compiler that pointers in it's scope are now available to call though global pointers.
template <typename ValueT>
inline void use(const ValueT& val) noexcept
{
    asm volatile("" : : "r,m"(val) : "memory");
}

/// The use function emulates the creation additional uses of the reference essentially instructing
/// the compiler that pointers in it's scope are now available to call though global pointers.
template <typename ValueT>
inline void use(ValueT& val) noexcept
{
#if defined(__clang__)
    asm volatile("" : "+r,m"(val) : : "memory");
#else
    asm volatile("" : "+m,r"(val) : : "memory");
#endif
}

struct TOOLBOX_API Runnable {
    virtual ~Runnable();
    virtual void run() = 0;
};

class TOOLBOX_API BenchmarkStore {
  public:
    static BenchmarkStore& instance();

    void store(const char* name, Runnable& runnable);
    void list() const;

    void run(const std::string& regex, bool randomise);

  private:
    BenchmarkStore();
    BenchmarkStore(const BenchmarkStore&) = delete;

    std::map<const char*, Runnable*> store_;
};

template <typename FuncT, class Enable = void>
class BenchmarkCaller {
  public:
    BenchmarkCaller(FuncT& func)
    : func_{func}
    {
    }

    void operator()() { func_(); }

    int calls_per_execution() const noexcept { return 1; }

  private:
    FuncT& func_;
};

template <typename FuncT>
class BenchmarkCaller<FuncT, typename std::enable_if_t<FunctionTraits<FuncT>::Arity>> {
  public:
    BenchmarkCaller(FuncT& func)
    : func_{func}
    {
    }

    void operator()()
    {
        for (auto curr = start; curr < end; ++curr) {
            use(func_(curr));
            clobber();
        }
    }

    auto calls_per_execution() const noexcept { return end - start; }

  private:
    FuncT func_;
    using Traits = FunctionTraits<FuncT>;
    using RangeType = typename Traits::template ArgType<0>;

  public:
    RangeType start;
    RangeType end;
};

template <typename FuncT>
class Benchmark : public Runnable {

  public:
    Benchmark() = delete;

    Benchmark(Benchmark&& b)
    : name_{b.name_}
    , func_{b.func_}
    {
        BenchmarkStore::instance().store(name_, *this);
    }

    Benchmark(const char* name, FuncT func)
    : name_{name}
    , func_{func}
    {
        BenchmarkStore::instance().store(name_, *this);
    }

    // Chaining breaks the lifetime extension of the rvalue and since we register the address in the
    // store in the constructor chaining methods need to return move, and move constructors also
    // register overriding the pointer value in the store.
    template <typename FromT, typename ToT>
        Benchmark range(const FromT& from, const ToT& to) && noexcept
    {
        func_.start = from;
        func_.end = to;
        return std::move(*this);
    }

    template <typename ValueT>
        Benchmark value(const ValueT& val) && noexcept
    {
        func_.start = val;
        // TODO: this should be a different type.
        func_.end = val + 1;
        return std::move(*this);
    }

    void run() override final
    {
        using namespace std::literals::chrono_literals;

        constexpr auto sleep = 2s;

        // Warmup get clock timings.
        // Measure single call.
        std::atomic_bool done{false};
        std::thread t([&]() {
            std::this_thread::sleep_for(sleep);
            done = true;
        });
        int runs{0};
        while (!done) {
            func_();
            runs++;
        }
        t.join();

        const long long expected_seconds{3};
        const long long n_calls = expected_seconds * runs / sleep.count();
        auto start_check = std::chrono::high_resolution_clock::now();
        for (int i = 0; i < n_calls; ++i) {
            func_();
        }
        auto end_check = std::chrono::high_resolution_clock::now();
        auto end_diff
            = std::chrono::duration_cast<std::chrono::nanoseconds>(end_check - start_check);
        std::cout << name_ << ' ' << end_diff.count() / (n_calls * func_.calls_per_execution())
                  << ' ' << n_calls << ' '
                  << std::chrono::duration_cast<std::chrono::microseconds>(end_diff).count()
                  << '\n';
    }

    const char* name_;

    BenchmarkCaller<FuncT> func_;
};

namespace detail {
TOOLBOX_API int main(int argc, char* argv[]);
}

} // namespace benchmark
} // namespace toolbox

#define TOOLBOX_BENCHMARK_OP_2(Count) reg_##Count
#define TOOLBOX_BENCHMARK_OP_1(Name, Func, Value)                                                  \
    static const auto& TOOLBOX_BENCHMARK_OP_2(Value) = toolbox::benchmark::Benchmark { Name, Func }
#define TOOLBOX_BENCHMARK(Name, Func) TOOLBOX_BENCHMARK_OP_1(Name, Func, __COUNTER__)

#define TOOLBOX_BENCHMARK_MAIN()                                                                   \
    int main(int argc, char* argv[]) { return toolbox::benchmark::detail::main(argc, argv); }

#endif // TOOLBOX_UTIL_BENCHMARK_HPP
