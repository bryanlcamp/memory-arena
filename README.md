# <span style="color:cornflowerblue">**Memory Arena:**</span>

This project is an example of how to create and use an ultra-low latency, cross-platform Memory Arena in C++23.  
  
The point is to illustrate how a sequential bump allocator eliminates the runtime latency penalties of standard OS memory management tools like malloc or new. By pre-allocating an entire chunk of page-aligned, pinned virtual memory up front during system boot, the hot path avoids dynamic boundary calculations, thread contention, and memory compaction cycles.  
  
As we know, memory arenas rely heavily on spatial and temporal locality. Specifically, when one piece of data is needed, nearby data is loaded into the L1/L2 cache at the same time. The hardware doesn't fetch a single byte from RAM; it grabs an entire hardware cache line, which is usually 64 bytes on x86 architectures or 128 bytes on Apple Silicon. By allocating objects right next to each other in a flat contiguous byte block, the CPU's prefetch engine can predict exactly what memory lines to warm up next. This minimizes cache misses and translation lookaside buffer stalls during tight execution cycles.  

## <span style="color:cornflowerblue">**Physical Limits and Quirks:**</span>
  
A common myth is that you can force an allocator to "live in the L3 cache." Caches are managed entirely by hardware microcode and cache-coherency protocols like MESI. Memory moves through the hierarchy based strictly on your thread confinement choices and data access frequency:  

* <u>The Backing Store:</u> The actual underlying byte array must be requested from the OS heap using VirtualAlloc on Windows or mmap on Linux and macOS. Trying to place megabytes or gigabytes of memory on a thread's stack will instantly cause a crash.  

* <u>The CPU Pipeline:</u> When an isolated execution core continuously mutates the active offset pointer boundary, those specific cache lines are pulled straight into that core's dedicated L1 and L2 structures.  

* <u>The L3 Bridge:</u> In an interconnect pipeline using single-producer single-consumer ring buffers, the L3 cache acts as the physical bridge. When the producer thread builds a data payload inside the arena and passes the pointer to the ring buffer, the data shifts from the producer's L1/L2 cache lines into the shared L3 cache, where the consumer thread can pull it into its own registers without touching system RAM.

## <span style="color:cornflowerblue">**Operational Realities:**</span>

To hit single-digit nanosecond execution speeds, this arena is built without internal atomics, spinlocks, or mutexes. It is meant to be strictly single-threaded and pinned to a dedicated hardware core. Deallocating individual items is completely unsupported; objects are created in-place using modern C++23 implicit lifetime features, decay naturally, and are cleared instantly when the entire arena offset drops back to zero at the end of a processing cycle.

## <span style="color:#6495ED">**Differential Latency Profiling**</span>

To isolate the exact cost of your business logic in a software-only simulation environment, the project generates two separate decoupled execution binaries alongside the allocator benchmark driver:

* **`baseline_no_work`**: Establishes the absolute raw transit floor of the system by measuring just the allocation cost and lock-free thread handover loop without touching data.  

* **`cme_sbe_demo`**: Measures the full operational cost of streaming, copy-serializing, and decoding true CME MDP 3.0 SBE Template 46 structures inside active registers.

By executing both tests sequentially inside your build sandbox, you can subtract the baseline transit latency from your total SBE parsing telemetry. This isolates the absolute computational cost of your parsing matrix down to the fraction of a nanosecond, removing OS scheduling jitter from your profiling.
Use code with caution.If you'd like, let me know:If you want to map out a custom .vscode/tasks.json configuration so you can toggle and execute these separate compilation profiles straight from your editor shortcuts.If you are ready to review your SpScRingBuffer.hpp file under your strict same-line brace rules to ensure it compiles with zero warnings under GCC 15!

## <span style="color:cornflowerblue">**Contact:**</span>

Bryan Camp  
bryancamp@gmail.com  
https://www.linkedin.com/in/bryanlcamp/