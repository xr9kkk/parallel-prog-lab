#include <iostream>
#include <vector>
#include <thread>
#include <future>
#define NOMINMAX
#include <windows.h>
#include <limits>
#include <algorithm>
#include <atomic>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <omp.h>
#include <random>

std::vector<int> data{};

std::vector<int> generate_random_data(size_t size = 20, int min_val = 1, int max_val = 100) {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> distrib(min_val, max_val);

    std::vector<int> result(size);
    for (auto& x : result) {
        x = distrib(gen);
    }
    return result;
}

// Общая функция поиска максимального нечётного числа
int find_max_odd(const std::vector<int>& arr) {
    int max_val = std::numeric_limits<int>::min();
    for (int val : arr) {
        if (val % 2 != 0) {
            max_val = std::max(max_val, val);
        }
    }
    return max_val;
}

///////////////////////
// 1. WinAPI Thread ///
///////////////////////
struct ThreadData {
    const std::vector<int>* arr;
    int result;
};

DWORD WINAPI winapi_thread_func(LPVOID param) {
    ThreadData* data = static_cast<ThreadData*>(param);
    data->result = find_max_odd(*data->arr);
    return 0;
}

int max_with_winapi() {
    ThreadData td{ &data, std::numeric_limits<int>::min() };
    HANDLE thread = CreateThread(nullptr, 0, winapi_thread_func, &td, 0, nullptr);
    WaitForSingleObject(thread, INFINITE);
    CloseHandle(thread);
    return td.result;
}

/////////////////////////
// 2. std::thread ///////
/////////////////////////
void thread_worker(const std::vector<int>& arr, int& result) {
    result = find_max_odd(arr);
}

int max_with_std_thread() {
    int result = std::numeric_limits<int>::min();
    std::thread t(thread_worker, std::cref(data), std::ref(result));
    t.join();
    return result;
}

/////////////////////////
// 3. std::async ////////
/////////////////////////
int max_with_future() {
    std::future<int> fut = std::async(std::launch::async, find_max_odd, std::cref(data));
    return fut.get();
}

/////////////////////////
// 4. std::atomic ///////
/////////////////////////
int max_with_atomic() {
    std::atomic<int> max_val(std::numeric_limits<int>::min());
    std::thread t([&]() {
        for (int val : data) {
            if (val % 2 != 0) {
                int current = max_val;
                while (val > current && !max_val.compare_exchange_weak(current, val));
            }
        }
        });
    t.join();
    return max_val;
}

/////////////////////////
// 5.Пул потоков + потокобезопасная очередь с объектами ядра
/////////////////////////
class SafeQueue {
    std::queue<int> queue;
    std::mutex mtx;
    std::condition_variable cv;

public:
    void push(int value) {
        std::lock_guard<std::mutex> lock(mtx);
        queue.push(value);
        cv.notify_one();
    }

    bool pop(int& value) {
        std::unique_lock<std::mutex> lock(mtx);
        if (queue.empty()) return false;
        value = queue.front();
        queue.pop();
        return true;
    }

    bool empty() {
        std::lock_guard<std::mutex> lock(mtx);
        return queue.empty();
    }
};

int max_with_thread_pool() {
    SafeQueue q;
    std::atomic<int> max_val(std::numeric_limits<int>::min());

    for (int val : data) {
        q.push(val);
    }

    auto worker = [&]() {
        int value;
        while (q.pop(value)) {
            if (value % 2 != 0) {
                int current = max_val;
                while (value > current && !max_val.compare_exchange_weak(current, value));
            }
        }
        };

    std::vector<std::thread> pool;
    for (int i = 0; i < 4; ++i) {
        pool.emplace_back(worker);
    }

    for (auto& t : pool) t.join();
    return max_val;
}

/////////////////////////
// 6.Critical Section ///
/////////////////////////
CRITICAL_SECTION cs;
std::queue<int> shared_queue;
bool finished = false;

DWORD WINAPI producer_func(LPVOID) {
    EnterCriticalSection(&cs);
    for (int val : data) {
        shared_queue.push(val);
    }
    finished = true;
    LeaveCriticalSection(&cs);
    return 0;
}

DWORD WINAPI consumer_func(LPVOID param) {
    int* max_val = static_cast<int*>(param);
    while (true) {
        EnterCriticalSection(&cs);
        if (!shared_queue.empty()) {
            int val = shared_queue.front(); shared_queue.pop();
            if (val % 2 != 0) {
                *max_val = std::max(*max_val, val);
            }
        }
        else if (finished) {
            LeaveCriticalSection(&cs);
            break;
        }
        LeaveCriticalSection(&cs);
    }
    return 0;
}

int max_with_critical_section() {
    InitializeCriticalSection(&cs);
    int max_val = std::numeric_limits<int>::min();

    HANDLE threads[2];
    threads[0] = CreateThread(nullptr, 0, producer_func, nullptr, 0, nullptr);
    threads[1] = CreateThread(nullptr, 0, consumer_func, &max_val, 0, nullptr);

    WaitForMultipleObjects(2, threads, TRUE, INFINITE);
    DeleteCriticalSection(&cs);

    CloseHandle(threads[0]);
    CloseHandle(threads[1]);
    return max_val;
}

/////////////////////////
// 7.OpenMP /////////////
/////////////////////////
static  int max_with_openmp() {
    int max_val = std::numeric_limits<int>::min();
#pragma omp parallel for reduction(max:max_val)
    for (int i = 0; i < data.size(); ++i) {
        if (data[i] % 2 != 0) {
            max_val = std::max(max_val, data[i]);
        }
    }
    return max_val;
}

/////////////////////////
// Main //////////////////
int main() {
    data = generate_random_data();
    std::cout << "Array: ";
    for (const int v : data) std::cout << v << " ";
    std::cout << "\n\n";

    std::cout << "1. Max odd (WinAPI)              : " << max_with_winapi() << "\n";
    std::cout << "2. Max odd (std::thread)         : " << max_with_std_thread() << "\n";
    std::cout << "3. Max odd (std::async)          : " << max_with_future() << "\n";
    std::cout << "4. Max odd (std::atomic)         : " << max_with_atomic() << "\n";
    std::cout << "5. Max odd (Safe queue)          : " << max_with_thread_pool() << "\n";
    std::cout << "6. Max odd (Critical section)    : " << max_with_critical_section() << "\n";
    std::cout << "7. Max odd (Open mp)             : " << max_with_openmp() << "\n";

    return 0;
}
