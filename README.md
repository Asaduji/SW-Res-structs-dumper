# SW Res structs dumper
A dirty but effective way of dumping RES file structs, reads the game binary to find function calls and figure out structs and dumps them in C# .cs file format.
## Usage
All you have to do is drag any file (for example SoulWorker.exe) from the game directory to the dumper .exe
## Compiling
This project uses [zydis](https://github.com/zyantific/zydis) to decode the x64 opcodes, you must have [vcpkg](https://github.com/Microsoft/vcpkg) installed in order to install the dependencies, then all you have to do is load with Visual Studio 2022 and compile.
