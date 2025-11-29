#pragma once
// #include "ThreadCache.h"
#include "ThreadCache.cpp"

// 相当于TCMalloc，线程调用这个函数申请空间
void* ConcurrentAlloc(size_t size)
{
    // 如果申请空间超过256KB，直接找下层的去要
    if (size > MAX_BYTES)
    {
        size_t alignSize = SizeClass::RoundUp(size);    // 按页大小对齐
        size_t k = alignSize >> PAGE_SHIFT;     // 对齐之后需要多少页

        PageCache::GetInstance()->_pageMtx.lock();  // 对pc中的span进行操作，加锁
        Span* span = PageCache::GetInstance()->NewSpan(k);  // 直接向pc申请k页
        span->_objSize = size;      
        PageCache::GetInstance()->_pageMtx.unlock();    // 解锁

        void* ptr = (void*)(span->_pageId << PAGE_SHIFT);   // 通过获得的span提供空间
        return ptr;
    }
    else
    {
        /* 因为pTLSThreadCache是TLS的，每个线程都会有一个，且相互独立，所以不存在竞争pTLSThreadCache的问题，
        所以这里只需要判断一次就可以直接new，不存在线程安全问题 */
        if (pTLSThreadCache == nullptr)
        {
            // pTLSThreadCache = new ThreadCache;     // 不用new（malloc）
            // 此时就相当于每个线程都有了一个ThreadCache对象

            // 用定长内存池来申请空间
            static ObjectPool<ThreadCache> objPool; // 静态的，一直存在
            objPool._poolMtx.lock();    // 加锁，不然多线程可能会申请到空指针
            pTLSThreadCache = objPool.New();    
            objPool._poolMtx.unlock();  // 解锁
        }

        // cout << std::this_thread::get_id() << " " << pTLSThreadCache << endl; 

        return pTLSThreadCache->Allocate(size);
    }
    
}

// 线程调用这个函数用来回收空间
void ConcurrentFree(void* ptr)
{
    assert(ptr);

    // 通过ptr找到对应的span，因为申请空间的时候已经保证维护的空间首地址映射过
    Span* span = PageCache::GetInstance()->MapObjectToSpan(ptr);
    size_t size = span->_objSize;   // 通过映射来的size获取ptr所指空间大小

    // 通过size判断是不是大于256KB
    if (size > MAX_BYTES)
    {

        PageCache::GetInstance()->_pageMtx.lock();
        PageCache::GetInstance()->ReleaseSpanToPageCache(span);
        PageCache::GetInstance()->_pageMtx.unlock();
    }
    else
    {
        pTLSThreadCache->Deallocate(ptr, size);
    }

}