# Mach1 🚀

**High-Performance Vector Database & Semantic Indexer**

Mach1 is a modern, enterprise-grade vector database designed for absolute maximum throughput and zero-overhead data ingestion. Built entirely on C++26, it leverages Data-Oriented Design (DOD) and strict RAII principles to manage high-dimensional AI embeddings (like LLM output vectors) with extreme efficiency.

## ⚡ Core Features

* **Zero-Copy Architecture:** Utilizes Linux memory mapping (`mmap`) to interact directly with disk storage at native RAM speeds. No massive heap allocations, no memory overhead.
* **Data-Oriented Design:** Embeddings are tightly packed in flat memory arenas to maximize CPU cache locality and alignment for SIMD vectorization.
* **Type-Safe Math Engine:** Implements `std::expected` for purely value-based, linear error handling without the stack-unwinding penalty of traditional C++ exceptions.
* **Modern C++26 Standard:** Built using the latest features, concepts, and standard library components.
* **Robust CLI Parsing:** Subcommand routing and automatic file validation powered by the industrial-grade `CLI11` framework.
* **Decoupled Toolchain:** Completely relies on Conan 2 to handle package provision and inject rigorous compiler diagnostics (`-Wall`, `-Wsign-conversion`, ASan/UBSan).

---

## 🛠️ Prerequisites

To build Mach1, your system must meet the following baseline requirements:

* **Compiler:** GCC 15.0+ (Requires full C++26 support)
* **Build System:** CMake 3.28 or higher
* **Package Manager:** Conan 2.x
* **OS:** Linux (Requires POSIX `mmap` and `ftruncate` APIs)

---

## 🏗️ Build Instructions

Mach1 uses a decoupled build system. CMake manages the target layouts, while Conan 2 orchestrates the dependencies and compiler flags via the `CMakeDeps` and `CMakeToolchain` generators.

**1. Install dependencies and generate CMake bindings:**
```bash
conan install . -pr=./conan/profiles/gcc-26-debug --build=missing
