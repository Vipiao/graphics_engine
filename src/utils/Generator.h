#pragma once
#include <iostream>
#include <memory>
#include <vector>
#include <coroutine>
#include <glm/glm.hpp>
#include <exception> 

/*
Example 1:

#include "Generator.h"
#include <iostream>

Generator<int> generateNumbers(int start, int end) {
    for (int i = start; i <= end; ++i) {
        co_yield i; // Yielding integers from start to end
    }
}

int main() {
    auto gen = generateNumbers(1, 5); // Generates numbers from 1 to 5

    while (gen) {
        std::cout << "Generated value: " << gen() << std::endl;
        ++gen; // Move to the next generated value
    }

    return 0;
}


Example 2.

#include "Generator.h"
#include <iostream>
#include <string>

class MyClass {
public:
    Generator<int> logic(int start, int end) {
        for (int i = start; i <= end; ++i) {
            co_yield i; // Yielding integers from start to end
        }
    }
};

int main() {
    MyClass myClass;
    auto gen = myClass.logic(1, 5); // Generates numbers from 1 to 5

    while (gen) {
        std::cout << "Generated value: " << gen() << std::endl;
        ++gen; // Move to the next generated value
    }

    return 0;
}
*/

template <typename T>
class Generator {
public:
   struct promise_type {
      //bool m_isFinished{ false };
      T value;
      std::exception_ptr exception{};
      std::suspend_always yield_value(T v) { value = v; return {}; }
      std::suspend_always initial_suspend() {
         return {};
      }
      std::suspend_always final_suspend() noexcept {
         return {};
      }
      void return_void() {
         //std::cout << "return_void()" << std::endl;
      }
      void unhandled_exception() {
         exception = std::current_exception();
      }
      Generator get_return_object() { return Generator{ Handle::from_promise(*this) }; }
   };

   struct Handle : std::coroutine_handle<promise_type> {
      Handle(std::coroutine_handle<promise_type> h) : std::coroutine_handle<promise_type>(h) {}
      Handle() : std::coroutine_handle<promise_type>() {}
      T& operator()() {
         if (this->promise().exception) {
            std::rethrow_exception(this->promise().exception);
         }
         return this->promise().value;
      }
      void operator++() {
         this->resume();
         if (this->promise().exception) {
            std::rethrow_exception(this->promise().exception);
         }
      }
   };

   Generator(Handle h) : handle(h) {}
   Generator(const Generator&) = delete;
   Generator& operator=(const Generator&) = delete;
   Generator(Generator&& other) noexcept : handle(std::move(other.handle)) {
      other.handle = Handle();
   }
   Generator& operator=(Generator&& other) noexcept {
      if (this != &other) {
         if (handle) handle.destroy();
         handle = std::move(other.handle);
         other.handle = Handle();
      }
      return *this;
   }
   ~Generator() { if (handle) handle.destroy(); }

   T& operator()() { return handle(); }
   void operator++() { ++handle; }
   explicit operator bool() {
      //return !handle.promise().m_isFinished;
      return !handle.done();
   }

private:
   Handle handle{};
};