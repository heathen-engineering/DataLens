# Sample toolchain file for cross-compiling to Windows using MinGW-w64 on Linux.
# You may need to install the mingw-w64 package on your distribution.
# (e.g. sudo apt install mingw-w64 on Ubuntu/Debian)

set(CMAKE_SYSTEM_NAME Windows)

# Specify the cross compiler
set(CMAKE_C_COMPILER x86_64-w64-mingw32-gcc)
set(CMAKE_CXX_COMPILER x86_64-w64-mingw32-g++)
set(CMAKE_RC_COMPILER x86_64-w64-mingw32-windres)

# Where is the target environment located?
set(CMAKE_FIND_ROOT_PATH  /usr/x86_64-w64-mingw32)

# Adjust the default behavior of the FIND_XXX() commands:
# Search programs in the host environment
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)

# Search headers and libraries in the target environment
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)

# Statically link the C/C++ runtimes (libgcc, libstdc++, libwinpthread) so the resulting
# datalens.dll is SELF-CONTAINED — no libstdc++-6.dll / libgcc_s_seh-1.dll / libwinpthread-1.dll
# need to ship alongside it. Essential so the Unity Foundation's vendored DLL just works on a
# clean machine. (For a SHARED target CMake still passes -shared, so this stays a DLL.)
set(CMAKE_EXE_LINKER_FLAGS_INIT    "-static")
set(CMAKE_SHARED_LINKER_FLAGS_INIT "-static")
