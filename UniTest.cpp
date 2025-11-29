#include "ConcurrentAlloc.h"

// 线程1执行方法
void Alloc1()
{   // 两个线程调用ConcurrentAlloc测试
    for (int i = 0; i < 5; ++i)
    {
        ConcurrentAlloc(6);
    }
}

// 线程2执行方法
void Alloc2()
{
    for (int i = 0; i < 5; ++i) 
    {
        ConcurrentAlloc(7);
    }
}

void AllocTest()
{
    std::thread t1(Alloc1);
    t1.join();

    std::thread t2(Alloc2);
    t2.join();
}

void ConcurrentAllocTest1()
{
    void* ptr1 = ConcurrentAlloc(5);
    void* ptr2 = ConcurrentAlloc(8);
    void* ptr3 = ConcurrentAlloc(4);
    void* ptr4 = ConcurrentAlloc(6);
    void* ptr5 = ConcurrentAlloc(3);

    cout << ptr1 << endl;
    cout << ptr2 << endl;
    cout << ptr3 << endl;
    cout << ptr4 << endl;
    cout << ptr5 << endl;
}

void ConcurrentAllocTest2()
{
    for (int i = 0; i < 1024; ++i)
    {
        void* ptr = ConcurrentAlloc(5);
        cout << ptr << endl;
    }

    void* ptr = ConcurrentAlloc(3);
    cout << "--------" << ptr << endl;
}

void TestConcurrentFree1()
{
    void* ptr1 = ConcurrentAlloc(5);
    void* ptr2 = ConcurrentAlloc(8);
    void* ptr3 = ConcurrentAlloc(4);
    void* ptr4 = ConcurrentAlloc(6);
    void* ptr5 = ConcurrentAlloc(3);
    void* ptr6 = ConcurrentAlloc(3);
    void* ptr7 = ConcurrentAlloc(3);

    cout << ptr1 << endl;
    cout << ptr2 << endl;
    cout << ptr3 << endl;
    cout << ptr4 << endl;
    cout << ptr5 << endl;
    cout << ptr6 << endl;
    cout << ptr7 << endl;

    ConcurrentFree(ptr1);
    ConcurrentFree(ptr2);
    ConcurrentFree(ptr3);
    ConcurrentFree(ptr4);
    ConcurrentFree(ptr5);
    ConcurrentFree(ptr6);
    ConcurrentFree(ptr7);
}

void MultiThreadAlloc1()
{
    std::vector<void*> vec;
    // 申请7次，正好单个线程能走到pc回收cc中span的那一步
    for (size_t i = 0; i < 7; ++i)
    {
        // 申请的都是8B的块空间
        void* ptr = ConcurrentAlloc(6);
        vec.push_back(ptr);
    }

    for (auto e : vec)
    {
        ConcurrentFree(e);
    }
}

void MultiThreadAlloc2()
{
    std::vector<void*> vec;
    for (size_t i = 0; i < 7; ++i)
    {
        // 申请的都是16B块空间
        void* ptr = ConcurrentAlloc(16); 
        vec.push_back(ptr);   
    }

    for (size_t i = 0; i < 7; ++i)
    {
        ConcurrentFree(vec[i]);
    }
}

void TestMultiThread()
{
    std::thread t1(MultiThreadAlloc1);
    std::thread t2(MultiThreadAlloc2);

    t1.join();
    t2.join();
}

void BigAlloc()
{
    void* p1 = ConcurrentAlloc(257 * 1024);
    ConcurrentFree(p1);

    void* p2 = ConcurrentAlloc(129 * 8 * 1024);
    ConcurrentFree(p2);
}

int main()
{
    // AllocTest(); 
    // ConcurrentAllocTest1();
    ConcurrentAllocTest2();

    // TestConcurrentFree1();
    // MultiThreadAlloc1();
    // MultiThreadAlloc2();

    // TestMultiThread();

    // BigAlloc();



    return 0;
}