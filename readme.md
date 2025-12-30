# Custom Memory Allocator: Architectural Analysis and Optimization

## 1. Overview
This project involves the design and implementation of a C-based custom memory allocator. The study focuses on evaluating the performance trade-offs between different heap management strategies. By transitioning from an initial Implicit List to an Explicit Segregated List combined with an Arena allocation strategy, the allocator achieved a significant throughput increase, ultimately surpassing the performance of the standard glibc malloc in high-load stress tests.

## 2. Performance Comparison

| Implementation Phase | Strategy | Throughput (ops/sec) | Relative Performance |
| :--- | :--- | :---: | :--- |
| **Phase 1** | **Implicit List** | 38,507 | ~24x Slower than glibc |
| **Phase 2** (Final) | **Explicit Segregated List + Arena** | **1,023,751** | **~10% Faster than glibc** |

The initial implementation was bottlenecked by $O(N)$ search complexity, where allocation time increased linearly with the number of heap blocks. The final optimized version achieved a **26x throughput increase** by implementing constant-time binning and eliminating runtime system calls.

---

## 3. Analysis of Failure Modes: Resolving Segmentation Faults

During the development of Phase 1, an incremental `sbrk` strategy (requesting memory in 2KB units) resulted in consistent Segmentation Faults after approximately 2,000 allocation cycles. Low-level debugging identified two primary system-level causes:

1. **VMA Fragmentation and sbrk Non-contiguity**: Frequent, small-unit heap expansions increased the complexity of Virtual Memory Area (VMA) management within the OS. In a fragmented address space, `sbrk` calls can return non-contiguous memory regions, breaking the pointer arithmetic logic that assumes the new chunk header follows the previous epilogue.
2. **Heap Exhaustion Handling**: The lack of robust handling for `sbrk(-1)` led to attempts to write metadata to invalid memory addresses during heap exhaustion events.

**Solution: 128MB Arena Allocation**
The allocator was redesigned to use a **128MB Arena** strategy. By pre-allocating a large, contiguous memory block during initialization, the system ensured memory layout stability and reduced the system-call overhead to zero during the benchmark execution.

---

## 4. Technical Implementation Details

### 4.1. Explicit Segregated List with Sentinel Nodes
The final version utilizes segregated bins to categorize free chunks by size. To optimize list manipulation, **Sentinel Nodes (Dummy Heads)** were implemented for every bin.
* **Branchless Operations**: This design eliminates conditional branches for empty or single-node lists, ensuring $O(1)$ operations for node insertion and removal.
* **Alignment**: All metadata and payloads are strictly aligned to 8-byte boundaries to satisfy hardware requirements and prevent alignment-related faults.



### 4.2. Boundary Tagging and Immediate Coalescing
Bi-directional coalescing is facilitated through `hdr` and `prev_size` boundary tags.
* **Integrity Verification**: Stress tests with 10,000 randomized cycles confirmed that the heap successfully recovers into a single 128MB free block without leakage.
* **Debug Output**: `mchunkptr [0x5f027392c000] | size: 134217728` (Validates 100% coalescing efficiency).

---

## 5. Conclusion
This project demonstrates that while the standard glibc malloc is engineered for general-purpose robustness, significant performance gains can be achieved for specific high-load workloads by **minimizing system-call frequency** and **optimizing data structures for search efficiency**. The transition from 38K to 1M ops/sec highlights the critical impact of algorithmic selection and OS interaction on systems software performance.

---

## 6. Usage
```bash
make
./bench_my   # Execute Custom Allocator Benchmark
./bench_std  # Execute Standard glibc malloc Benchmark
