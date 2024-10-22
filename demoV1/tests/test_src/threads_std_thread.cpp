#include <thread>
#include <chrono>
#include <iostream>
#include <format>

inline void time_waster(std::chrono::milliseconds ms)
{
    volatile int* ptr = (volatile int*)malloc(sizeof(int));
    *ptr = 1;

    auto t = std::chrono::system_clock::now();  
    while(ptr)
    {
        if(std::chrono::system_clock::now() - t > ms)     
        {
            break;
        }
    }
}

int main(int argc, char** argv)
{
    std::cout << std::thread::hardware_concurrency() << " concurrent threads are supported\n";
    size_t threads = 5;
    std::chrono::milliseconds time_per_thread(2000);

/*     for(int i = 0; i < threads; ++i)
    {
        std::jthread thread(time_waster, time_per_thread);
    } */
    std::thread t1(time_waster, time_per_thread);
    std::thread t2(time_waster, time_per_thread);
    std::thread t3(time_waster, time_per_thread);
    std::thread t4(time_waster, time_per_thread);
    std::thread t5(time_waster, time_per_thread);

//    std::this_thread::sleep_for(time_per_thread);
    t1.join();
    t2.join();
    t3.join();
    t4.join();
    t5.join();
    std::cout << std::format("succesfully wasted {} ms of cpu time", threads*time_per_thread.count()) << std::endl;
}
    