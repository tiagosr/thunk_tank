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

#if defined(__APPLE__) || defined(__LINUX__)
#include <unistd.h>
#include <sys/mman.h>
#elif defined(_WIN32) || defined(__WIN32__)

#include <windows.h>

#endif

#if defined(__x86_64__) || defined(__arm64__)
#define REG_SIZE 8
#else
#define REG_SIZE 4
#endif

template <typename ...>
class thunk_tank_is_vararg {};

template <typename Ret, typename... ArgTypes>
class thunk_tank_is_vararg<Ret(ArgTypes...)> {
public:
    static constexpr bool value = false;
};

template <typename Ret, typename... ArgTypes>
class thunk_tank_is_vararg<Ret(ArgTypes..., ...)> {
public:
    static constexpr bool value = true;
};


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
    
    template <typename T>
    struct stack_fit_size:
        std::conditional<
            (sizeof(T) > REG_SIZE),
            size_t_c<sizeof(T)>,
            size_t_c<REG_SIZE>>::type {};
    
    // compute stack size for _cdecl and _stdcall type calls
    using stack_args_size = count<stack_fit_size<ArgTypes>::value...>;
    
    // compute amount of arguments that will be put in integral/pointer registers
    template <typename T>
    struct int_ptr_arg_item:
        std::conditional<
            (std::is_integral<T>::value || std::is_pointer<T>::value) && (sizeof(T)<=REG_SIZE),
            size_t_c<1>,
            size_t_c<0>
        >::type {};
    template <typename T>
    struct float_arg_item:
        std::conditional<
            std::is_floating_point<T>::value,
            size_t_c<1>,
            size_t_c<0>
        >::type {};
    
    using int_ptr_args_count = count<int_ptr_arg_item<ArgTypes>::value...>;
    using float_args_count = count<float_arg_item<ArgTypes>::value...>;
    
    static constexpr uint8_t byte_of(void* ptr, size_t byte) {
        return ((intptr_t)ptr >> (byte * 8)) & 255;
    }
    static constexpr uint8_t byte_of(int i, size_t byte) {
        return (i >> (byte * 8)) & 255;
    }
    
    struct thunk_patch {
#if defined(__x86_64__)
# if defined(_WIN64) // why does windows have to do this differently?
        uint8_t bytes[64];
        
        thunk_patch(ofxThunk* thunk) {
            constexpr uint8_t movabsq_reg_indices[] = {
                0x48, 0xba, // rdx
                0x48, 0xb9, // rcx
                0x48, 0xbf, // rdi
                0x48, 0xbe, // rsi
                0x49, 0xb8, // r9
                0x49, 0xb9 // r8
            };
            if(int_ptr_args_count::value < 6) {
                void *call = (void*)std::addressof(thunk_tank::inner_call);
                uint8_t _bytes = {
                    0x48, 0x29, 0x24, 0x25, 0x20, 0x00, 0x00, 0x00, // subq %rsp, 0x20 (pad stack with 32 bytes
                    movabsq_reg_indices[int_ptr_args_count::value*2],
                    movabsq_reg_indices[int_ptr_args_count::value*2+1],
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
            } else {
                void *call = (void*)std::addressof(thunk_tank::inner_call_tf_pad7);
                uint8_t _bytes[64] = {
                    0x58, // pop %rax -> save return address
                    0x41, 0x51, // push %r9 -> push 6th argument to the stack
                    0x41, 0x50, // push %r8 -> push 5th argument
                    0x56, // push %rsi -> push 4th argument
                    0x57, // push %rdi -> push 3rd argument
                    0x51, // push %rcx -> push 2nd argument
                    0x52, // push %rdx -> push 1st argument
                    0x50, // push %rax -> push return address to the vacant spots in the stack
                    0x50, // push %rax -> align to 16-byte address
                    0x48, 0xbf,
                    byte_of(thunk, 0), byte_of(thunk, 1), byte_of(thunk, 2), byte_of(thunk, 3),
                    byte_of(thunk, 4), byte_of(thunk, 5), byte_of(thunk, 6), byte_of(thunk, 7),
                    // movabsq #*thunk, %rdx //
                    0x48, 0xb8,
                    byte_of(call, 0), byte_of(call, 1), byte_of(call, 2), byte_of(call, 3),
                    byte_of(call, 4), byte_of(call, 5), byte_of(call, 6), byte_of(call, 7),
                    // movabsq #thunk_tank::inner_call_tf_pad7, %rax
                    0xff, 0xd0, // call *%rax
                    0x5f, // pop %rdi -> pop return address to temp register
                    // (an add #56, %rsp would do, but would be more bytes than the next 7 insns)
                    0x5e, // pop %rsi -> remove moved argument
                    0x5e, // pop %rsi -> remove moved argument
                    0x5e, // pop %rsi -> remove moved argument
                    0x5e, // pop %rsi -> remove moved argument
                    0x5e, // pop %rsi -> remove moved argument
                    0x5e, // pop %rsi -> remove moved argument
                    0x5e, // pop %rsi -> remove moved argument
                    0x57, // push %rdi -> push back return address
                    0xc3 // ret
                };
                memcpy(bytes, _bytes, 64);
            }
        }
# else
        uint8_t bytes[64];
        thunk_patch(thunk_tank* thunk) {
            if(int_ptr_args_count::value < 6) {
                constexpr uint8_t movabsq_reg_indices[] = {
                    0x48, 0xbf, // rdi
                    0x48, 0xbe, // rsi
                    0x48, 0xba, // rdx
                    0x48, 0xb9, // rcx
                    0x49, 0xb8, // r9
                    0x49, 0xb9 // r8
                };
                void *call = (void*)std::addressof(thunk_tank::inner_call);
                // we can put the thunk pointer in a register
                uint8_t _bytes[32] = {
                    0x57, // push %rdi (to align stack to 16 bytes
                    movabsq_reg_indices[int_ptr_args_count::value*2],
                    movabsq_reg_indices[int_ptr_args_count::value*2+1],
                    byte_of(thunk, 0), byte_of(thunk, 1), byte_of(thunk, 2), byte_of(thunk, 3),
                    byte_of(thunk, 4), byte_of(thunk, 5), byte_of(thunk, 6), byte_of(thunk, 7),
                    // movabsq #*thunk, reg
                    0x48, 0xb8,
                    byte_of(call, 0), byte_of(call, 1), byte_of(call, 2), byte_of(call, 3),
                    byte_of(call, 4), byte_of(call, 5), byte_of(call, 6), byte_of(call, 7),
                    // movabsq #thunk_tank::inner_call, %rax
                    0xff, 0xd0, // call *%rax
                    0x5f, // pop %rdi
                    0xc3 // ret
                };
                memcpy(bytes, _bytes, 32);
            } else {
                void *call = (void*)std::addressof(thunk_tank::inner_call_tf_pad7);
                uint8_t _bytes[64] = {
                    0x58, // pop %rax -> save return address
                    0x41, 0x51, // push %r9 -> push 6th argument to the stack
                    0x41, 0x50, // push %r8 -> push 5th argument
                    0x51, // push %rcx -> push 4th argument
                    0x52, // push %rdx -> push 3rd argument
                    0x56, // push %rsi -> push 2nd argument
                    0x57, // push %rdi -> push 1st argument
                    0x50, // push %rax -> push return address to the vacant spots in the stack
                    0x50, // push %rax -> align to 16-byte address
                    0x48, 0xbf,
                    byte_of(thunk, 0), byte_of(thunk, 1), byte_of(thunk, 2), byte_of(thunk, 3),
                    byte_of(thunk, 4), byte_of(thunk, 5), byte_of(thunk, 6), byte_of(thunk, 7),
                    // movabsq #*thunk, %rdx
                    0x48, 0xb8,
                    byte_of(call, 0), byte_of(call, 1), byte_of(call, 2), byte_of(call, 3),
                    byte_of(call, 4), byte_of(call, 5), byte_of(call, 6), byte_of(call, 7),
                    // movabsq #thunk_tank::inner_call_tf_pad7, %rax
                    0xff, 0xd0, // call *%rax
                    0x5f, // pop %rdi -> pop return address to temp register
                    // (an add #56, %rsp would do, but would be more bytes than the next 7 insns)
                    0x5e, // pop %rsi -> remove moved argument
                    0x5e, // pop %rsi -> remove moved argument
                    0x5e, // pop %rsi -> remove moved argument
                    0x5e, // pop %rsi -> remove moved argument
                    0x5e, // pop %rsi -> remove moved argument
                    0x5e, // pop %rsi -> remove moved argument
                    0x5e, // pop %rsi -> remove moved argument
                    0x57, // push %rdi -> push back return address
                    0xc3 // ret
                };
                memcpy(bytes, _bytes, 64);
            }
        }
# endif
#elif defined(__i386__)||defined(_X86_)
# if defined(_WIN32)||(__WIN32__)
        
        // __cdecl ABI: every argument is passed through the stack, return is eax:edx
        uint8_t bytes[32];
        thunk_patch(thunk_tank* thunk) {
            void *call = (void*)std::addressof(thunk_tank::inner_call_static_pad);
            uint8_t _bytes[32] = {
                0x8b, 0x0d, // mov *thunk, %ecx
                byte_of(thunk, 0), byte_of(thunk, 1), byte_of(thunk, 2), byte_of(thunk, 3),
                0x51, // push %ecx
                0x8b, 0x0d, // mov *call, %ecx
                byte_of(call, 0), byte_of(call, 1), byte_of(call, 2), byte_of(call, 3),
                0xff, 0xd1, // call %ecx
                0x5b, // pop %ebx - remove the thunk pointer
                0xc3, // ret
            };
            memcpy(bytes, _bytes, 32);
        }
# else
        // SystemV ABI: every argument is passed through the stack, return is eax
        uint8_t bytes[32];
        thunk_patch(thunk_tank* thunk) {
            void *call = (void*)std::addressof(thunk_tank::inner_call_static_pad);
            uint8_t _bytes[32] = {
                0x8b, 0x0d, // mov *thunk, %ecx
                byte_of(thunk, 0), byte_of(thunk, 1), byte_of(thunk, 2), byte_of(thunk, 3),
                0x51, // push %ecx
                0x8b, 0x0d, // mov *call, %ecx
                byte_of(call, 0), byte_of(call, 1), byte_of(call, 2), byte_of(call, 3),
                0xff, 0xd1, // call %ecx
                0x5b, // pop %ebx - remove the thunk pointer
                0xc3, // ret
            };
            memcpy(bytes, _bytes, 32);
        }
# endif
#elif defined(__arm64__)
        uint8_t bytes[128];
        thunk_patch(thunk_tank* thunk) {
            void *call = (void*)std::addressof(thunk_tank::inner_call_tf_pad5);
            uint8_t _bytes[128] = {
                
            };
            memcpy(bytes, _bytes, 128);
        }

#elif defined(__ARM_ARCH_7__)
        uint8_t bytes[40];
        thunk_patch(thunk_tank* thunk) {
            void *call = (void*)std::addressof(thunk_tank::inner_call_tf_pad5);
            uint32_t _bytes[10] = {
                0xe92d400f, // push {r0, r1, r2, r3, lr} - move registers out of the way
                0xe59f0010, // ldr r0, [thunk]
                0xe59f1010, // ldr r1, [call]
                0xe12fff31, // blx r1 - call thunk
                0xe8bd400e, // pop {r1, r2, r3, lr}
                0xe49d1004, // ldr r1, [sp], #4
                0xe12fff1e, // bx lr - return to caller
                (uint32_t)(void*)thunk, // [thunk]
                (uint32_t)call, // [call]
            };
            memcpy(bytes, _bytes, 40);
        }
#elif defined(__ARM_ARCH_6__)
        uint8_t bytes[40];
        thunk_patch(thunk_tank* thunk) {
            void *call = (void*)std::addressof(thunk_tank::inner_call);
            uint32_t _bytes[10] = {
                0xe92d400f, // push {r0, r1, r2, r3, lr} - move registers out of the way
                0xe59f0010, // ldr r0, [thunk]
                0xe59f1010, // ldr r1, [call]
                0xebfffffe, // bl r1 - call thunk
                0xe8bd400e, // pop {r1, r2, r3, lr}
                0xe49d1004, // ldr r1, [sp], #4
                0xeafffffe, // b lr - return to caller
                (uint32_t)(void*)thunk, // [thunk]
                (uint32_t)call, // [call]
            };
            memcpy(bytes, _bytes, 40);
        }
#endif
    } __attribute__((__packed__));

    
    thunk_patch *patch;
    
    void setup() {
        void *allocpatch = nullptr;

#if defined(__APPLE__) || defined(__LINUX__)
         // alloc inside a page boundary
        //int ret = posix_memalign(&allocpatch, getpagesize(), sizeof(thunk_patch));
        // mprotect() will trap on subsequent allocations if we allocate less than
        // the whole page: better think of a sort of "pool" for those allocations
        int ret = posix_memalign(&allocpatch, getpagesize(), getpagesize());
        if(ret == 0) {
            patch = new(allocpatch)thunk_patch(this);
            int err = mprotect(patch, getpagesize(), PROT_READ|PROT_EXEC);
            if(err) {
                free(allocpatch);
                patch = NULL;
            }
        }
#elif defined(_WIN32) || defined(__WIN32__) || defined(_WIN64) || defined(__WIN64__)
        allocpatch = VirtualAlloc(NULL, sizeof(thunk_patch), MEM_RESERVE|MEM_COMMIT, PAGE_EXECUTE_READWRITE);
        if(allocpage) {
            patch = new(allocpatch)thunk_patch(this);
            FlushInstructionCache(GetCurrentProcess(), allocpatch, sizeof(thunk_patch));
        }
#endif
    }
    void unwind() {
        if(patch) {
#if defined(__APPLE__) || defined(__LINUX__)
            mprotect(patch, getpagesize(), PROT_READ|PROT_WRITE);
            patch->~thunk_patch();
            free(patch);
            patch = nullptr;
#elif defined(_WIN32) || defined(__WIN32__) || defined(_WIN64) || defined(__WIN64__)
            VirtualFree((void*)patch, sizeof(thunk_patch), MEM_DECOMMIT);
            patch = nullptr;
#endif
        }
    }
    
    std::function<Ret(ArgTypes...)> _callback;
    static __cdecl Ret inner_call(ArgTypes... args, thunk_tank* thunk) {
        return thunk->_callback(args...);
    }
    static __cdecl Ret inner_call_tf(thunk_tank* thunk, ArgTypes... args) {
        return thunk->_callback(args...);
    }
    static __cdecl Ret inner_call_tf_pad5(thunk_tank* thunk,
                                          void*, void*, void*, void*, void*,
                                          ArgTypes... args) {
        return thunk->_callback(args...);
    }
    static __cdecl Ret inner_call_tf_pad7(thunk_tank* thunk,
                                          void*, void*, void*, void*, void*, void*, void*,
                                          // pad with 7 arguments to remove x86-64 arguments
                                          // from registers and hide the saved return address
                                          ArgTypes... args) {
        return thunk->_callback(args...);
    }
    static __cdecl Ret inner_call_static_pad(thunk_tank* thunk, void*,
                                             // the unused argument is the return address
                                             ArgTypes... args) {
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

