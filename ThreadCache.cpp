#include "ThreadCache.h"
#include "CentralCache.cpp"
#include "PageCache.cpp"

void* ThreadCache::Allocate(size_t size)    // 线程申请size大小的空间
{
    assert(size <= MAX_BYTES);  // ThreadCache单次只能申请不超过256KB的空间

    size_t alignSize = SizeClass::RoundUp(size);    // size对齐后的字节数
    size_t index = SizeClass::Index(size);          // size对应在哈希表中的哪个桶

    // cout << "index: " << index << ", alignSize: " << alignSize << endl; 

    if (!_freeLists[index].Empty()) 
    {   // 自由链表不为空，可以直接从自由链表中获取空间
        return _freeLists[index].Pop();
    }
    else
    {   // 自由链表为空，需要向CentralCache申请空间
        // alignSize参数意味着 向CentralCache申请空间的时候不需要考虑对齐问题，直接申请整块大小
        return FetchFromCentralCache(index, alignSize);
    }
}

void ThreadCache::Deallocate(void* obj, size_t size)   // 回收线程中大小为size的obj空间
{
   assert(obj); // 回收的空间不能为空
   assert(size <= MAX_BYTES);   // 回收空间大小不能超过256KB

   size_t index = SizeClass::Index(size);   // 找到size对应的自由链表
   _freeLists[index].Push(obj);     // 用对应自由链表回收空间

    // 当前桶中的块数大于等于单批次申请块数的时候归还空间
    if (_freeLists[index].Size() >= _freeLists[index].MaxSize())
    {
        ListTooLong(_freeLists[index], size);
    }
}

// ThreadCache中空间不够时，向CentralCache申请空间的接口
void* ThreadCache::FetchFromCentralCache(size_t index, size_t alignSize)
{// 慢开始反馈调节算法

    // 通过MaxSize和NumMoveSize来控制当前给tc提供多少块alignSize大小的空间
    size_t batchNum = std::min(_freeLists[index].MaxSize(), SizeClass::NumMoveSize(alignSize));
    /*  MaxSize表示index位置的自由链表单次申请未到上限时，能够申请的最大块空间是多少
        NumMoveSize表示tc单次向cc申请alignSize大小的空间块的最多块数是多少
        二者取小，得到的就是本次要给tc提供多少块alignSize大小的空间
        例如alignSize=8B，MaxSize=1，NumMoveSize=512，那就要给一块8B空间
        即 没到上限就给MaxSize，到了上限就给上限的NumMoveSize
    */

    if (batchNum == _freeLists[index].MaxSize())
    {// 如果没有达到上限，那下次再申请这块空间的时候，可以多申请一块
        _freeLists[index].MaxSize()++;  // 慢开始反馈调节算法的核心
    }

    // 输出型参数，返回之后的结果就是tc想要的空间
    void* start = nullptr;
    void* end = nullptr;

    // cout << "batchNum: " << batchNum << endl;
    // 返回值为实际获取到的块数
    size_t actulNum = CentralCache::GetInstance()->FetchRangeObj(start, end, batchNum, alignSize);

    // actualNum一定是大于等于1的，这是FetchRangeObj能保证的
    assert(actulNum >= 1);

    if (actulNum == 1)
    {// 如果actulNum等于1，直接将start返回给线程
        assert(start == end);
        return start;
    }
    else
    {// 如果actulNum大于1，还要给tc对应位置插入[ObjNext(start), end]的空间
        _freeLists[index].PushRange(ObjNext(start), end, actulNum - 1);

        // 给线程返回start所指向的空间
        return start;
    }
}

// tc向cc归还空间
void ThreadCache::ListTooLong(FreeList& list, size_t size)
{
    void* start = nullptr;
    void* end = nullptr;

    // 获取MaxSize块空间
    list.PopRange(start, end, list.MaxSize());

    // 归还空间
    CentralCache::GetInstance()->ReleaseListToSpans(start, size);
}