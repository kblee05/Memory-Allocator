# 🚀 High-Performance Custom Memory Allocator

This project is a high-performance **Custom Memory Allocator** designed to outperform the standard **glibc malloc** in specific workloads (bulk allocation/free) by implementing an **Explicit Segregated List** and an **Arena Allocation** strategy.

## 📊 Performance Optimization: 26x Leap
Through iterative optimization, the allocator achieved a **26x performance increase** over the initial model, ultimately surpassing the throughput of the standard glibc library.

| Implementation Stage | Key Strategy | Throughput (ops/sec) | Remarks |
| :--- | :--- | :---: | :--- |
| **Phase 1** | **Implicit List** | 38,507 | ~24x slower than glibc |
| **Phase 2** | **Explicit Segregated List + Arena** | **1,023,751** 🏆 | **Outperformed glibc (926,312)** |

---

## 🔍 Technical Analysis: The 26x Speedup

### 1. Implicit vs. Explicit Segregated List
* **Implicit List (Phase 1)**: Utilized a sequential search ($O(N)$) through all chunks, leading to significant performance degradation as the number of allocations increased.
* **Explicit Segregated List (Phase 2)**: Maintained pointers only for free chunks using **Segregated Bins** categorized by size, minimizing search overhead.
* **$O(1)$ Optimization**: Introduced **Sentinel Nodes (Dummy Heads)** for every bin to eliminate conditional branching (`if` statements) during node insertion/removal, achieving true constant-time operations.



### 2. Incremental sbrk vs. 128MB Arena Allocation
* **Fragmented sbrk (Phase 1)**: Frequent 2KB-unit `sbrk` calls incurred heavy **System Call Overhead** (context switching) and caused potential heap boundary instability.
* **Arena Strategy (Phase 2)**: Pre-allocated a contiguous **128MB Arena** during initialization, reducing system calls to zero during the benchmark. All allocations are fulfilled via **Split/Fuse** operations within user-space, maximizing raw performance.

---

## 🌟 Integrity & Stability Verification

### [Performance Benchmark]
Comparison between the standard glibc malloc and the Custom Allocator under identical stress tests.
![Benchmark Comparison](./trial_1.png)

### [Full Coalescing Verification]
Verified that the heap perfectly recovers into a single 128MB block after 10,000 randomized allocation/free cycles, proving the robustness of the coalescing logic.

```text
heap top: 0x5f027b92c010
[HEAP START]
segment 9
mchunkptr [0x5f027392c000] | size: 134217728 | status: FREE
[HEAP END]
