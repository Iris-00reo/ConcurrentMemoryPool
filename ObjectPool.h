#pragma once

/* 定长内存池*/

#include <iostream>
using std::cout;
using std::endl;

/*
#ifdef _WIN32
    #include <Windows.h>    // Windows下的头文件
#else
    // Linux相关的头文件，这里忽略
#endif  // _WIN32

// 直接去堆上按页申请空间
inline static void* SystemAlloc(size_t kpage)
{
#ifdef _WIN32   // Windows下的系统调用接口
    void* ptr = VirtualAlloc(0, kpage << 13, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
#else
    // linux下brk mmap等
#endif

    if (ptr == nullptr) 
        throw std::bad_alloc();
    
    return ptr;
}
*/

template<class T>
class ObjectPool
{
public:
    T* New()    // 申请一个T类型大小的空间
    {
        T* obj = nullptr;   // 最终返回的空间

        if (_freelist)   // _freelist不为空，表示有回收T大小的小块可以重复利用
        {
            // 头删法
            void* next = *(void**)_freelist;
            obj = (T*)_freelist;
            _freelist = next;
        }
        else // 自由链表中没有块，即没有可以重复利用的空间
        {
            // _memory中剩余空间小于T大小的时候才开空间
            if (_remanentBytes < sizeof(T))     // 这样也包含剩余空间为0的情况
            {
                _remanentBytes = 128 * 1024;    // 向系统申请128K的空间
                
                _memory = (char*)malloc(_remanentBytes);  
                // _memory = (char*)SystemAlloc(_remanentBytes >> 13); // 使用系统调用接口申请16页内存
                
                if (_memory == nullptr) {   // 申请失败了抛出异常
                    throw std::bad_alloc();
                }
            }

                obj = (T*)_memory;  // 给定一个T类型的大小
                // 判断一下T的大小，小于指针就给一个指针大小，大于指针就还是T的大小
                size_t objSize = sizeof(T) < sizeof(void*) ? sizeof(void*) : sizeof(T);
                _memory += objSize;   // _memory后移一个T类型的大小
                _remanentBytes -= objSize;    // 空间给出后_remanetBytes减少了T类型的大小
        }
    
        new(obj)T;  // 通过定位new调用构造函数进行初始化

        return obj;
    }

    void Delete(T* obj)     // 回收归还回来的小空间
    {
        // 显式调用析构函数进行清理工作
        obj->~T();
        /*
        if (_freelist == nullptr)   // _freelist为空的时候
        {
            *(void**)obj = nullptr; // 前指针个字节空间指向nullptr
            _freelist = obj;    // obj连接到_freelist后面
        }
        else    // _freelist不为空的时候 
        {
            *(void**)obj = _freelist;   // 新块指向旧块
            _freelist = obj;    // 头指针指向新块
        }
        */
       // 头插法
       *(void**)obj = _freelist;
       _freelist = obj;
    }

public:
    std::mutex _poolMtx;    // 防止ThreadCache申请时申请到空指针

private:
    char* _memory = nullptr;    // 指向内存块的指针
    size_t _remanentBytes = 0;  // 大块内存在切分过程中的剩余字节数
    void* _freelist = nullptr;  // 自由链表，用来连接归还的空闲空间
};

struct TreeNode // 一个树结构的节点
{
    int _val;
    TreeNode* _left;
    TreeNode* _right;

    TreeNode() : _val(0), _left(nullptr), _right(nullptr) {}
};

void TestObjectPool()   // malloc和当前定长内存池性能对比
{
    const size_t Rounds = 5;    // 申请释放的轮次
    const size_t N = 100000;    // 每轮申请释放多少次
    // 总共申请和释放的次数即Rounds * N次，测试这么多次谁更快

    std::vector<TreeNode*> v1;
    v1.reserve(N);

    // 测试malloc的性能
    size_t begin1 = clock();
    for (size_t j = 0; j < Rounds; ++j) 
    {
        for (int i = 0; i < N; ++i) 
        {
            v1.push_back(new TreeNode); // 这里虽然用的是new，但new底层用的也是malloc
        }
        for (int i = 0; i < N; ++i) 
        {
            delete v1[i];   // 同样delete底层也是free
        }
        v1.clear(); // clear作用是将vector中的内容清空，size置零
        // 但capacity保持不变，这样才能循环上去重新push_back
    }
    size_t end1 = clock();

    std::vector<TreeNode*> v2;
    v2.reserve(N);

    // 定长内存池，其中申请和释放的T类型就是树节点
    ObjectPool<TreeNode> TNPool;
    size_t begin2 = clock();
    for (size_t j = 0; j < Rounds; ++j)
    {
        for (int i = 0; i < N; ++i) 
        {
            v2.push_back(TNPool.New()); // 定长内存池中申请空间
        }
        for (int i = 0; i < N; ++i) 
        {
            TNPool.Delete(v2[i]);   // 定长内存池中的回收空间
        }
        v2.clear();
    }
    size_t end2 = clock();


    cout << "new cost time: " << end1 - begin1 << endl;
    cout << "object pool cost time: " << end2 - begin2 << endl;
}