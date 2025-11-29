#include "CentralCache.h"
#include "PageCache.h"

CentralCache CentralCache::_sInst;  // CentralCache的饿汉对象


size_t CentralCache::FetchRangeObj(void*& start, void*& end, size_t batchNum, size_t size)
{
    // 获取到size对应哪一个SpanList
    size_t index = SizeClass::Index(size);

    // 对cc中的SpanList操作时需要加锁
    _spanLists[index]._mtx.lock();

    // 获取到一个管理空间非空的span
    Span* span = GetOneSpan(_spanLists[index], size);
    assert(span);   // 断言一下不为空
    assert(span->_freeList);    // 断言一下span管理的空间不为空

    // 起初都指向_freeList, 让end不断往后走
    start = end = span->_freeList;
    size_t actualNum = 1;   // 函数实际的返回值

    // 在end的next不为空的前提下，让end走batchNum - 1步
    size_t i = 0;
    while (i < batchNum - 1 && ObjNext(end) != nullptr)
    {
        end = ObjNext(end);
        ++actualNum;    // 记录end走了多少步
        ++i;
    }

    // 将[start, end]返回给ThreadCache后，调整Span的_freeList
    span->_freeList = ObjNext(end);
    span->_usecount += actualNum;   // 给tc分了多少就给_usecount加多少
    ObjNext(end) = nullptr; // 返回一段空间，不要和原先Span的_freeList中的块相连


    _spanLists[index]._mtx.unlock();

    return actualNum;
}

// 获取一个管理空间的非空Span
Span* CentralCache::GetOneSpan(SpanList& list, size_t size)
{
    // 先在cc中找一下有没有管理空间非空的span
    Span* it = list.Begin();
    while (it != list.End())
    {
        // 找到管理空间非空的span
        if (it->_freeList != nullptr)
        {
            return it;
        }
        else    // 没找到继续往下找
        {
            it = it->_next;
        }
    }

    // 解掉桶锁，让其他向该cc桶进行操作的线程能拿到锁
    list._mtx.unlock();

    // 走到这就是cc中没有找到管理空间非空的span

    // 将size转换成匹配的页数，以供pc提供一个合适的span
    size_t k = SizeClass::NumMovePage(size);

    // cout << "size: " << size << ", k: " << k << endl;

    // 解决死锁的方法三：在调用newSpan的地方加锁
    PageCache::GetInstance()->_pageMtx.lock();  // 加锁
    // 调用NewSpan获取一个全新span
    Span* span = PageCache::GetInstance()->NewSpan(k);
    span->_isUse = true;    // cc获取到了pc中的span，改成正在使用
    span->_objSize = size;  // 记录span被切分的块大小
    PageCache::GetInstance()->_pageMtx.unlock();    // 解锁

    // 因为_pageID是PageID类型（size_t或者unsigned long long)的，不能直接赋值给指针
    char* start = (char*)(span->_pageId << PAGE_SHIFT);
    char* end = (char*)(start + (span->_n << PAGE_SHIFT));

    // 开始切分span管理的空间
    span->_freeList = start;    // 管理的空间放到span->_freeList中

    void* tail = start;     // 起初让tail指向start
    start += size;          // start往后移一块，方便控制循环

    // 链接各个块
    while (start < end)
    {
        ObjNext(tail) = start;
        start += size;
        tail = ObjNext(tail);
    }
    ObjNext(tail) = nullptr;    // 将最后一块置空

    // 切好span之后，需要把span挂到cc对应下标的桶里面去
    list._mtx.lock();       // span挂上去之前加锁
    list.PushFront(span);

    return span;
}

// 将tc归还的多块空间放到span中
void CentralCache::ReleaseListToSpans(void* start, size_t size)
{
    // 先通过size找到对应的桶在哪里
    size_t index = SizeClass::Index(size);

    // 下面要对cc中的span进行操作，所以要加上cc的桶锁
    _spanLists[index]._mtx.lock();

    // 遍历start，将各个块放到对应页的span所管理的_freeList中
    while (start)   // start为空停止
    {
        // 记录一下start下一位
        void* next = ObjNext(start);

        // 找到对应的span
        Span* span = PageCache::GetInstance()->MapObjectToSpan(start);

        // 把当前块插入到对应span中
        ObjNext(start) = span->_freeList;
        span->_freeList = start;

        // 归还一块空间，对应span的usecount要减1
        --span->_usecount;
        if (span->_usecount == 0)   // 这个span管理的所有页都回来了
        {// 将这个span交给pc管理

            // 先将span从cc中删除
            _spanLists[index].Erase(span);
            span->_freeList = nullptr;
            span->_next = nullptr;
            span->_prev = nullptr;

            // 归还span，解掉当前桶锁
            _spanLists[index]._mtx.unlock();

            // 归还span，加上page的整体锁
            PageCache::GetInstance()->_pageMtx.lock();
            PageCache::GetInstance()->ReleaseSpanToPageCache(span);
            PageCache::GetInstance()->_pageMtx.unlock();

            // 归还完毕，再加上当前桶的桶锁
            _spanLists[index]._mtx.lock();
        }


        // 换下一个块
        start = next;
    }

    _spanLists[index]._mtx.unlock();
}