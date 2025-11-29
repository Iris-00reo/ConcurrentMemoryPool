#include "PageCache.h"

PageCache PageCache::_sInst;    // 单例对象

// pc从_spanLists中拿出来一个k页的span
Span* PageCache::NewSpan(size_t k)
{
    // // 申请页数一定是在[1, PAGE_NUM - 1]这个范围内
    // assert(k > 0 && k < PAGE_NUM);
    assert(k > 0);

    // 如果单次申请的页数超过128页，需要向OS申请，如果没有超过128页还可以向pc申请
    if (k > PAGE_NUM - 1)
    {
        void* ptr = SystemAlloc(k);     // 直接向os申请
        // Span* span = new Span;  // 开一个新的span，用来管理新的空间
        Span* span = _spanPool.New();

        span->_pageId = ((PageID)ptr >> PAGE_SHIFT);    // 申请空间的对应页号
        span->_n = k;   // 申请了多少页

        // 把这个span管理的首页映射到哈希中，后面删除这个span的时候能找到
        _idSpanMap[span->_pageId] = span;
        // 不需要把这个span交给pc管理，pc只能管小于128页的span

        return span;
    }

    // ① k号桶中有span
    if (!_spanLists[k].Empty()) 
    {
        // 直接返回该桶中的第一个span
        Span* span = _spanLists[k].PopFront();

        // 记录分配出去的span管理的页号和其地址的映射关系
        for (PageID i = 0; i < span->_n; ++i) 
        {
            // n页的空间全部映射都是span地址
            _idSpanMap[span->_pageId + i] = span;
        }

        return span;
    }

    // ② k号桶没有span，但后面的桶中有span
    for (int i = k + 1; i < PAGE_NUM; ++i) 
    {// 检查[k+1, PAGE_NUM]中有没有span

        if (!_spanLists[i].Empty())
        {// i号桶中有span，对该span进行切分

            // 获取到该桶中的span，起名nSpan
            Span* nSpan = _spanLists[i].PopFront();

            // 将这个span切分成一个k页的和一个n-k页的span

            // Span的空间是需要新建的，而不是用当前内存池中的空间
            // Span* kSpan = new Span;
            Span* kSpan = _spanPool.New();  //用定长内存池开空间

            // 分成一个k页的Span
            kSpan->_pageId = nSpan->_pageId;
            kSpan->_n = k;

            // 和一个 n-k 页的span
            nSpan->_pageId += k;
            nSpan->_n -= k;

            // 将n-k页的放回对应的哈希桶中
            _spanLists[nSpan->_n].PushFront(nSpan);

            // 再把n-k页的span边缘页映射一下，方便后续合并
            _idSpanMap[nSpan->_pageId] = nSpan;
            _idSpanMap[nSpan->_pageId + nSpan->_n - 1] = nSpan; 

            // 记录分配出去的span管理的页号和其地址的映射关系
            for (PageID i = 0; i < kSpan->_n; ++i) 
            {
                // n页的空间全部映射都是span地址
                _idSpanMap[kSpan->_pageId + i] = kSpan;
            }

            return kSpan;
        }
    }

    // ③ k号桶和后面的桶中都没有span

    // 直接向系统申请128页的span
    void* ptr = SystemAlloc(PAGE_NUM - 1);  // PAGE_NUM为129

    // 开一个新的span来维护这块空间山南投资
    // Span* bigSpan = new Span;
    Span* bigSpan = _spanPool.New();    // 用定长内存池开空间

    // 只需要修改_pageId和_n即可，系统调用接口申请空间的时候一定能保证申请的空间是对齐的
    bigSpan->_pageId = ((PageID)ptr) >> PAGE_SHIFT;
    bigSpan->_n = PAGE_NUM - 1;
    

    // 将这个span放到对应的哈希桶中
    _spanLists[PAGE_NUM - 1].PushFront(bigSpan);

    // 递归再次申请k页的span，这次递归一定会走②的逻辑
    return NewSpan(k);  // 复用代码
}

// 通过页地址找到span
Span* PageCache::MapObjectToSpan(void* obj)
{
    // 通过块地址找到页号
    PageID id = (((PageID)obj) >> PAGE_SHIFT);

    // 智能锁
    std::unique_lock<std::mutex> lc(_pageMtx);
    
    // 通过哈希表找到页号对应的span
    auto ret = _idSpanMap.find(id);

    // 这里的逻辑是一定能保证通过块地址找到一个span，如果没找到就出错了
    if (ret != _idSpanMap.end())
    {
        return ret->second;
    }
    else
    {
        assert(false);
        return nullptr;
    }
}

// 管理cc归还回来的span
void PageCache::ReleaseSpanToPageCache(Span* span)
{
    // 通过span判断释放的看空间页数是否大于128页，如果大于128页就直接还给os
    if (span->_n > PAGE_NUM - 1)
    {
        void* ptr = (void*)(span->_pageId << PAGE_SHIFT);   // 计算释放的地址
        size_t size = span->_n << PAGE_SHIFT;       // 计算释放空间大小：页数 * 每页大小
        SystemFree(ptr, size);
        // delete span;    // 释放span管理对象
        _spanPool.Delete(span); // 用定长内存池删除span

        return;
    }

    // 释放空间页数小于128页
    // 向左不断合并
    while (true)
    {
        PageID leftID = span->_pageId - 1;  // 拿到左边相邻页
        auto ret = _idSpanMap.find(leftID); // 通过相邻页映射出对应的span

        // 没有相邻span，停止合并
        if (ret == _idSpanMap.end())
        {
            break;
        }

        // 相邻span在cc中，停止合并
        Span* leftSpan = ret->second;   // 相邻span
        if (leftSpan->_isUse)
        {
            break;
        }

        // 相邻span与当前span合并后超过128页，停止合并
        if (leftSpan->_n + span->_n > PAGE_NUM - 1) 
        {
            break;
        }

        // 相邻span与当前span合并
        span->_pageId = leftSpan->_pageId;
        span->_n += leftSpan->_n;

        // 将相邻span对象从桶中删除
        _spanLists[leftSpan->_n].Erase(leftSpan);
        // delete leftSpan;  
        _spanPool.Delete(leftSpan); // 用定长内存池删除span
    }

    // 向右不断合并
    while (true)
    {
        PageID rightID = span->_pageId + span->_n;
        auto ret = _idSpanMap.find(rightID);

        // 没有相邻span，停止合并
        if (ret == _idSpanMap.end())
        {
            break;
        }

        // 相邻span在cc中，停止合并
        Span* rightSpan = ret->second;
        if (rightSpan->_isUse)
        {
            break;
        }

        // 相邻span与当前span合并后超过128页，停止合并
        if (rightSpan->_n + span->_n > PAGE_NUM - 1)
        {
            break;
        }

        // 相邻span与当前span合并，往右边合并时不需要修改span->_pageId，右边的会直接拼在span后面
        span->_n += rightSpan->_n;

        // 把桶里的span删掉
        _spanLists[rightSpan->_n].Erase(rightSpan);
        // delete rightSpan
        _spanPool.Delete(rightSpan);    // 用定长内存池删除span
    }

    // 合并完毕，将当前span挂到对应桶中
    _spanLists[span->_n].PushFront(span);
    span->_isUse = false;   // 从cc返回pc，isUse改成false

    // 映射当前span的边缘页，后续还可以对这个span合并
    _idSpanMap[span->_pageId] = span;
    _idSpanMap[span->_pageId + span->_n - 1] = span;

}