# thunk_tank
Have a stubborn library that only provides _their own data_ and doesn't leave you any place to put a pointer to ```this``` or anything similar? Not even a context pointer you could put as a key into a hash table and dispatch from there? You're welcome.
This is a header-only C++11 function thunk type for use with C/C++/whatever APIs that don't provide user data or context pointers.

Currently the type supports X86-64 on the SystemV ABI, with Windows support being worked on. On the short term, I plan on supporting ARMv6 and ARMv7 calling conventions too.

## Example

``` c++
...
// library code
typedef void(*some_library_callback_fn)(int sensor_value_or_whatever); // why no userdata/context?? whyyyy??
typedef char(*some_library_return_callback_fn)(void); // hopeless...
void some_library_set_callback(some_library_callback_fn callback); // why no userdata?? whyyyy??
void some_library_set_another_callback(some_library_return_callback_fn return_callback);
...

...
// our code
#include "thunk_tank.h"

my_object obj;
...
thunk_tank<void(int)> my_thunk([&](int sensor_value) {
    // go crazy with it!
    // create this thunk within a method and you have the *this pointer available
    // capture variables with the [] up there to keep your userdata
    // even return data
    obj->do_something_with_sensor_value(sensor_value);
});
thunk_tank<int(void)> my_return_thunk(char[&]() {
    // have to return something to that horrible horrible library? here you go.
    return obj->my_char_value;
});
...
// setting up the thunks
some_library_callback_fn thunked_callback = my_thunk.thunk(); // keep an eye on the thunk object scope - when my_thunk gets destroyed the thunk code is freed
some_library_return_callback_fn thunked_return_callback = my_return_thunk.thunk();

some_library_set_callback(thunked_callback); // awww yeahhhh
some_library_set_return_callback(thunked_return_callback); // awww yeahhhh
...
```

## Considerations

Should you find this code useful, I wouldn't mind a little shoutout on your commit message or on twitter. Something like this:
```
My project was saved from infinite headaches by @tiagosr's thunk_thank. Thanks!
```
would be much appreciated =)
