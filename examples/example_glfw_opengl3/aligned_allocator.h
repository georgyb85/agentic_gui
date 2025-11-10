#pragma once
#include <vector>
#include <memory>
#include <cstdlib>

template<typename T, std::size_t Alignment = 64>
class AlignedAllocator {
public:
    typedef T value_type;
    typedef T* pointer;
    typedef const T* const_pointer;
    typedef T& reference;
    typedef const T& const_reference;
    typedef std::size_t size_type;
    typedef std::ptrdiff_t difference_type;

    template<typename U>
    struct rebind {
        typedef AlignedAllocator<U, Alignment> other;
    };

    AlignedAllocator() noexcept = default;
    
    template<typename U>
    AlignedAllocator(const AlignedAllocator<U, Alignment>&) noexcept {}

    pointer allocate(size_type n) {
        if (n == 0) return nullptr;
        
        size_type bytes = n * sizeof(T);
        void* ptr = nullptr;
        
#ifdef _WIN32
        ptr = _aligned_malloc(bytes, Alignment);
        if (!ptr) throw std::bad_alloc();
#else
        int result = posix_memalign(&ptr, Alignment, bytes);
        if (result != 0) throw std::bad_alloc();
#endif
        
        return static_cast<pointer>(ptr);
    }

    void deallocate(pointer p, size_type) noexcept {
        if (p) {
#ifdef _WIN32
            _aligned_free(p);
#else
            std::free(p);
#endif
        }
    }

    template<typename U, std::size_t A>
    bool operator==(const AlignedAllocator<U, A>&) const noexcept {
        return true;
    }

    template<typename U, std::size_t A>
    bool operator!=(const AlignedAllocator<U, A>& other) const noexcept {
        return !(*this == other);
    }
};

template<typename T>
using AlignedVector = std::vector<T, AlignedAllocator<T, 64>>;