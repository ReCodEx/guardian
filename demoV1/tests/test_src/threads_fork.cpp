#include <errno.h>
#include <cassert>
#include <sys/types.h>
#include <sys/wait.h>
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
    free((void*)ptr);
}

static int waster_spawner(int depth, size_t count, const std::chrono::milliseconds& time_per_thread)
{
    pid_t child_pid;        
    if(depth)
    {
        std::cout << "Spawning another spawner!" << std::endl;
        child_pid = fork();
        if(child_pid < 0)
        {
            std::cout << std::format("fork() at depth {} failed with errno {}", -depth, errno) << std::endl;
            exit(0);
        }
        else if(child_pid == 0)
        {
            waster_spawner(--depth, count, time_per_thread);
            exit(0);
        }
        else
        {
            int stat{};
            pid_t p = waitpid(child_pid, &stat, 0);
            if(p < 0)
            {
                std::cout << std::format("waitpid() at depth {} failed with errno {}", -depth, errno) << std::endl;
                exit(0);
            }
        }
    }
    std::thread t1(time_waster, time_per_thread);
    std::thread t2(time_waster, time_per_thread);
    std::thread t3(time_waster, time_per_thread);
    std::thread t4(time_waster, time_per_thread);
    std::thread t5(time_waster, time_per_thread);

    t1.join();
    t2.join();
    t3.join();
    t4.join();
    t5.join();
    
    std::cout << std::format("Spawner at depth -{} succesfully wasted {} ms of cpu time!", depth, count*time_per_thread.count()) << std::endl;
    int stat{};
    return 0;
}
int main(int argc, char** argv)
{
    std::cout << std::thread::hardware_concurrency() << " concurrent threads are supported\n";

    size_t threads = 5;
    size_t depth = 5;
    std::chrono::milliseconds time_per_thread(100);
    waster_spawner(depth, threads, time_per_thread);
    std::cout << std::format("succesfully wasted {} ms ({}*{}*{}) of CPU time\n", (depth+1)*threads*time_per_thread.count(), depth + 1, threads, time_per_thread.count()); 
}
    