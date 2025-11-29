/* 比较的时候分两种情况
   1. 申请ntimes * rounds 次同一块大小的空间
   2. 申请ntimes * rounds 次不同的块大小的空间 
*/

#include "ConcurrentAlloc.h"

/*
    ntimes: 一轮申请和释放内存的次数
    rounds: 轮次
    nworks:  表示创建多少个线程
*/

void BenchmarkMalloc(size_t ntimes, size_t rounds, size_t nworks)
{
    std::vector<std::thread> vthread(nworks);
    std::atomic<size_t> malloc_costtime(0);
    std::atomic<size_t> free_costtime(0);

    for (size_t k = 0; k < nworks; ++k)
    {
        vthread[k] = std::thread([&, k]() {
            std::vector<void*> v;
            v.reserve(ntimes);

            for (size_t j = 0; j < rounds; ++j)
            {
                size_t begin1 = clock();
                for (size_t i = 0; i < ntimes; ++i)
                {
                    v.push_back(malloc(16));    // 每次申请同一个桶中的块
                    // v.push_back(malloc((16 + i) % 8192 + 1));   // 每次申请不同桶中的块
                }
                size_t end1 = clock();

                size_t begin2 = clock();
                for (size_t i = 0; i < ntimes; ++i)
                {
                    free(v[i]);
                }
                size_t end2 = clock();
                v.clear();

                malloc_costtime += (end1 - begin1);
                free_costtime += (end2 - begin2);
            }
        });
    }

    for (auto& t : vthread)
    {
        t.join();
    }

    printf("%zu个线程并发执行%zu轮次，每轮次malloc %zu次：花费：%lu ms\n",
        nworks, rounds, ntimes, malloc_costtime.load());

    printf("%zu个线程并发执行%zu轮次，每轮次free %zu次：花费：%lu ms\n",
        nworks, rounds, ntimes, free_costtime.load());

    printf("%zu个线程并发malloc & free %zu次：总计花费：%lu ms\n",
        nworks, nworks * rounds * ntimes, malloc_costtime.load() + free_costtime.load());

}

void BenchmarkConcurrentMalloc(size_t ntimes, size_t nworks, size_t rounds)
{
    std::vector<std::thread> vthread(nworks);
    std::atomic<size_t> malloc_costtime(0);
    std::atomic<size_t> free_costtime(0);

    for (size_t k = 0; k < nworks; ++k)
    {
        vthread[k] = std::thread([&]() {
            std::vector<void*> v;
            v.reserve(ntimes);

            for (size_t j = 0; j < rounds; ++j)
            {
                size_t begin1 = clock();
                for (size_t i = 0; i < ntimes; ++i)
                {
                    v.push_back(ConcurrentAlloc(16));
                    // v.push_back(ConcurrentAlloc((16 + i) % 8192 + 1));
                }
                size_t end1 = clock();

                size_t begin2 = clock();
                for (size_t i = 0; i < ntimes; ++i)
                {
                    ConcurrentFree(v[i]);
                }
                size_t end2 = clock();
                v.clear();

                malloc_costtime += (end1 - begin1);
                free_costtime += (end2 - begin2);
            }
        });
    }

    for (auto& t : vthread)
    {
        t.join();
    }

    printf("%zu个线程并发执行%zu轮次，每轮次concurrent alloc %zu次：花费：%lu ms\n",
        nworks, rounds, ntimes, malloc_costtime.load());
        
    printf("%zu个线程并发执行%zu轮次，每轮次concurrent dealloc %zu次：花费：%lu ms\n",
        nworks, rounds, ntimes, free_costtime.load());

    printf("%zu个线程并发concurrent alloc & dealloc %zu次：总计花费：%lu ms\n",
        nworks, nworks * rounds * ntimes, malloc_costtime.load() + free_costtime.load());
}


int main()
{
    size_t n = 10000;
    cout << "--------------------------------------" << endl;
    // 内存池：4个线程，每个线程申请10万次，总计申请40万次
    BenchmarkConcurrentMalloc(n, 4, 10);
    cout << endl << endl;

    // 传统malloc：4个线程，每个线程申请10万次，总计申请40万次
    BenchmarkMalloc(n, 4, 10);
    cout << "-------------------------------------" << endl;

    return 0;
}