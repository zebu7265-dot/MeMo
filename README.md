# MeMo - Memory Kernel

This repository contains an in-memory, immutable, MVCC-capable Memory Kernel implemented in modern C++ (C++17/C++20 compatible). The implementation provides:

- Immutable MemoryObject hierarchy (Fact, Event, Belief, Procedure, Execution)
- MemoryGraph with MVCC Transactions
- ProcedureEngine with native function registry and composite procedures
- Example main demonstrating usage

Build with a C++17 compatible compiler. Example (g++):

    g++ -std=c++17 -pthread main.cpp -o memo

