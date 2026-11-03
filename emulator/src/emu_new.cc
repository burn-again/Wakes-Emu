// emu_new.cc -- operator new/delete for the wasm build.
//
// The firmware is built without exceptions and never allocates on the heap after boot;
// Ubuntu's wasm32 libc++abi defines these in terms of __cxa_throw, which it does not
// ship. Allocation failure aborts, as it would on the device.
#include <cstdlib>
#include <new>

void *operator new(std::size_t n) { void *p = std::malloc(n ? n : 1); if (!p) { std::abort(); } return p; }
void *operator new[](std::size_t n) { return operator new(n); }
void *operator new(std::size_t n, const std::nothrow_t &) noexcept { return std::malloc(n ? n : 1); }
void *operator new[](std::size_t n, const std::nothrow_t &) noexcept { return std::malloc(n ? n : 1); }
void operator delete(void *p) noexcept { std::free(p); }
void operator delete[](void *p) noexcept { std::free(p); }
void operator delete(void *p, std::size_t) noexcept { std::free(p); }
void operator delete[](void *p, std::size_t) noexcept { std::free(p); }
void operator delete(void *p, const std::nothrow_t &) noexcept { std::free(p); }
void operator delete[](void *p, const std::nothrow_t &) noexcept { std::free(p); }
