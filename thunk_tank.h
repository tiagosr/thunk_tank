//
// Thunk Tank
// generic thunks for callback functions
// of libraries that don't give user-data
// pointers or contexts as parameters
//
// (c)2016 @tiagosr
//
// released under the MIT license

#pragma once

#include <type_traits>
#include <functional>
#include <array>
#include <memory>
#include <cstdint>
#include <cstdlib>

#include <unistd.h>
#include <sys/mman.h>

#if defined(__x86_64__)
#define REG_SIZE 8
#else
#define REG_SIZE 4
#endif

template <typename ...>
class thunk_tank {};

template <typename Ret, typename... ArgTypes>
class thunk_tank<Ret(ArgTypes...)> {
private:
    template <size_t C>
    using size_t_c = std::integral_constant<size_t, C>;
    
    template <size_t ...>
    struct count: size_t_c<0> {};
    template <size_t S, size_t ... Rest>
    struct count<S, Rest...>: size_t_c<S+count<Rest...>::value> {};
    
    // compute stack size for _cdecl and _stdcall type calls
    template <typename... Args>
    struct stack_args_size: count<sizeof(Args)...> {};
    
    // compute amount of arguments that will fit in integral/pointer registers
    template <typename T>
    struct int_ptr_arg_item:
        std::conditional<
            (std::is_integral<T>::value || std::is_pointer<T>::value) && (sizeof(T)<=REG_SIZE),
            size_t_c<1>,
            size_t_c<0>
        >::type {};
    
    template <typename... Args>
    struct int_ptr_args_count: count<int_ptr_arg_item<Args>::value...> {};
    
    
    static constexpr uint8_t byte_of(void* ptr, size_t byte) {
        return ((intptr_t)ptr >> (byte * 8)) & 255;
    }
    
    struct thunk_patch {
        uint8_t bytes[64]; // 64 bytes of trampoline code should be enough for everyone, right? right?
#if defined(__x86_64__)
#if  defined(_WIN64) // why does windows have to do this differently?
        
        thunk_patch(ofxThunk* thunk, size_t argpos) {
            constexpr uint8_t movabsq_reg_indices[] = {
                0x48, 0xba, // rdx
                0x48, 0xb9, // rcx
                0x48, 0xbf, // rdi
                0x48, 0xbe, // rsi
                0x49, 0xb8, // r9
                0x49, 0xb9 // r8
            };
            void *call = (void*)std::addressof(thunk_tank::inner_call);
            uint8_t _bytes = {
                0x48, 0x29, 0x24, 0x25, 0x20, 0x00, 0x00, 0x00, // subq %rsp, 0x20 (pad stack with 32 bytes
                movabsq_reg_indices[argpos*2],
                movabsq_reg_indices[argpos*2+1],
                byte_of(thunk, 0), byte_of(thunk, 1), byte_of(thunk, 2), byte_of(thunk, 3),
                byte_of(thunk, 4), byte_of(thunk, 5), byte_of(thunk, 6), byte_of(thunk, 7),
                // movabsq thunk, reg (will be patched in with init)
                0x48, 0xb8,
                byte_of(call, 0), byte_of(call, 1), byte_of(call, 2), byte_of(call, 3),
                byte_of(call, 4), byte_of(call, 5), byte_of(call, 6), byte_of(call, 7),
                // movabsq ofxThunk::inner_call, %rax (will also be patched)
                0xff, 0xd0, // call *%rax
                0x48, 0x01, 0x24, 0x25, 0x20, 0x00, 0x00, 0x00, // addq %rsp, 0x20 (unpad stack back)
                0xc3 // ret
            };
            memcpy(bytes, _bytes, 64);
        }
#else
        thunk_patch(thunk_tank* thunk, size_t argpos) {
            constexpr uint8_t movabsq_reg_indices[] = {
                0x48, 0xbf, // rdi
                0x48, 0xbe, // rsi
                0x48, 0xba, // rdx
                0x48, 0xb9, // rcx
                0x49, 0xb8, // r9
                0x49, 0xb9 // r8
            };
            void *call = (void*)std::addressof(thunk_tank::inner_call);
            uint8_t _bytes[64] = {
                0x57, // push %rdi (to align stack to 16 bytes
                movabsq_reg_indices[argpos*2],
                movabsq_reg_indices[argpos*2+1],
                byte_of(thunk, 0), byte_of(thunk, 1), byte_of(thunk, 2), byte_of(thunk, 3),
                byte_of(thunk, 4), byte_of(thunk, 5), byte_of(thunk, 6), byte_of(thunk, 7),
                // movabsq #*thunk, reg
                0x48, 0xb8,
                byte_of(call, 0), byte_of(call, 1), byte_of(call, 2), byte_of(call, 3),
                byte_of(call, 4), byte_of(call, 5), byte_of(call, 6), byte_of(call, 7),
                // movabsq #thunk_tank::inner_call, %rax
                0xff, 0xd0, // call *%rax
                0x5f, // pop %rd
                0xc3 // ret
            };
            memcpy(bytes, _bytes, 64);
        }
#endif
#elif defined(__i386__)||defined(_X86_)
#if  defined(_WIN32)||(__WIN32__)
        
#else
        
#endif
#elif defined(__ARM_ARCH_7__)
#elif defined(__ARM_ARCH_6__)
#endif
    } __attribute__((__packed__));

    
    thunk_patch *patch;
    
    void setup() {
        void *allocpatch = NULL;
         // alloc inside a page boundary
        //int ret = posix_memalign(&allocpatch, getpagesize(), sizeof(thunk_patch));
        // mprotect() will trap on subsequent allocations if we allocate less than
        // the whole page: better think of a sort of "pool" for those allocations
        int ret = posix_memalign(&allocpatch, getpagesize(), getpagesize());
        if(ret == 0) {
            patch = new(allocpatch)thunk_patch(this, int_ptr_args_count<ArgTypes...>::value);
            int err = mprotect(patch, sizeof(thunk_patch), PROT_READ|PROT_EXEC);
            if(err) {
                free(allocpatch);
                patch = NULL;
            }
        }
    }
    void unwind() {
        if(patch) {
            patch->~thunk_patch();
            free(patch);
        }
    }
    
    std::function<Ret(ArgTypes...)> _callback;
    static __cdecl Ret inner_call(ArgTypes... args, thunk_tank* thunk) {
        return thunk->_callback(args...);
    }
public:
    typedef Ret (*call_spec)(ArgTypes...);
    thunk_tank(std::function<Ret(ArgTypes...)> callback):
        _callback(callback), patch(0) {
        setup();
    }
    ~thunk_tank() {
        unwind();
    }
    call_spec thunk() const {
        return reinterpret_cast<call_spec>((void*)patch);
    }
};

