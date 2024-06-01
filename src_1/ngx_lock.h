// 定义了一个名为 ngx_lock 的锁类，它使用 C++11 标准库中的 std::atomic_flag 来实现一个简单的自旋锁（spinlock）。这是一个非常轻量级的锁机制，适用于短时间内的锁定操作，避免了线程在等待锁时进入睡眠状态，从而减少了上下文切换的开销。

#pragma once

#include "ngx_core.h"

class ngx_lock
{
private:
    std::atomic_flag flag; // std::atomic_flag 是一个原子类型，专门用于实现简单的标志（flag）。它可以用来实现锁和同步机制。默认状态是清除的（unset）。

public:
    ngx_lock() : flag(ATOMIC_FLAG_INIT){}; // 使用初始化列表将 flag 初始化为 ATOMIC_FLAG_INIT。这将 flag 设置为未设置状态。

    ~ngx_lock() = default;

    void lock()
    {
        // flag.test_and_set(std::memory_order_acquire)：这个原子操作会设置 flag 并返回其先前的值。如果 flag 已经被设置（表示锁已被占用），则继续循环
        while (flag.test_and_set(std::memory_order_acquire))
        {
            // std::this_thread::yield()：在锁被占用时，提示操作系统将当前线程从 CPU 上调度下来，使其他线程能够运行。这是为了避免自旋锁占用 CPU 太久。
            std::this_thread::yield(); // 提示操作系统进行线程切换
        }
    }

    void unlock()
    {
        //flag.clear(std::memory_order_release)：这个原子操作会清除 flag（解锁），并使用 std::memory_order_release 内存顺序确保所有先前的写入操作对其他线程可见。
        flag.clear(std::memory_order_release);
    }
};


// 这段代码实现了一个简单的自旋锁类 ngx_lock，利用 std::atomic_flag 进行原子操作，确保线程安全。
// lock() 方法通过自旋等待（不断尝试获取锁）和 std::this_thread::yield() 函数来提示操作系统进行线程调度。
// unlock() 方法通过清除 std::atomic_flag 来释放锁，确保内存顺序的正确性。
// 这种自旋锁适用于锁定时间很短的场景，因为自旋等待会持续占用 CPU。如果锁定时间较长，使用自旋锁可能会导致性能问题，应考虑使用其他锁机制如互斥锁（std::mutex）等。