# jitcc
Toy JIT C compiler library

Just-in-time C compiler heavily based and inspired by [chibicc](https://github.com/rui314/chibicc) and LLVM 2.0.

**Disclaimer**: This is an unfinished experiment. It should not be used in a production environment or for any other serious use. You are better off using any other C (JIT) compiler.

### Goal

There is no end goal other than learn how compilers work. In other words, it's just another project for me to abandon :)

### What's not included:

- C Preprocessor
    - WIP; missing include, pragma, error, etc.
- Optional C99 features (VLAs, atomic ops, etc.)
- GNU C extensions
- Floating point IR/codegen
    - WIP
- Many other major or minor things I cannot recall at the moment. Don't expect your random C code to be compilable!

### What's included:

- C frontend (jcc.c)
    - Heavily based on [chibicc](https://github.com/rui314/chibicc) with some changes/fixes from [slimcc](https://github.com/fuhsnn/slimcc/tree/main)
    - I've rewritten most of the code for educational purposes but if you are familiar with chibicc/slimcc you'll be able to navigate your way through the code.
- High-level Intermediate Representation (jir.c, jir_pass.c, jir_gen.c)
    - IR inspired by LLVM 2.0.
    - It's not LLVM! It's more or less what I understood by reading LLVM's source code and adapting it to what I thought I needed.
- Machine-level Intermediate Representation (jmir.c, jmir_pass.c, jmir_gen.c)
    - It only supports x86_64 (AMD64).
    - There is no provision to support anything else at the moment.
    - Some ABI specific constructs might be unintentionally hardcoded for Windows x64 ABI, because I develop on Windows and this is my main target.
- JIT assembler (jit.c)
    - Simple x86_64 instruction encoder.

### 3rd party code

- chibicc is copyright (c) 2019 Rui Ueyama
- slimcc is copyright (c) 2023 Hsiang-Ying Fu
- stb_sprintf.h is copyright (c) 2017 Sean Barrett
- vurtun/json.h is written by Micha Mettke
- c-testsuite is copyright (c) 2018 Andrew Chambers
- any other 3rd party code is copyright by their respective owners
