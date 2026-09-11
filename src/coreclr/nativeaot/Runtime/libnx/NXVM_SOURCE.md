# Bounded allocator source

`nxvm.c` and `nxvm.h` originate in the
[dotnet-switch allocator](https://github.com/pixelomer/dotnet-switch).
The allocator implementation is included in this source tree; building it does
not require a separate checkout or prebuilt runtime. Its data-only reservations
do not support executable permissions or arbitrary fixed addresses.
