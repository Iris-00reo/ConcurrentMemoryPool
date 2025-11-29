#pragma once
#include "Common.h"

class CentralCache
{
public:
    // 单例接口
    static CentralCache* GetInstance()
    {
        return &_sInst;
    }

    // CentralCache从自己的_spanLists中为ThreadCache提供所需要的块空间
    /*  start和end表示存储提供的空间开始和结尾，输出型参数
        n表示tc需要多少块size大小的空间
        size表示tc需要的单块空间的大小
        返回值是cc实际提供的空间大小
    */
    size_t FetchRangeObj(void*& start, void*& end, size_t batch_Num, size_t size);
    
    // 获取一个管理空间不为空的span
    Span* GetOneSpan(SpanList& list, size_t size);

    // 将tc归还回来的多块空间放到span中
    void ReleaseListToSpans(void* start, size_t size);

private:
    // 构造函数私有化
    CentralCache() {}

    // 删除拷贝构造函数、赋值运算符重载函数
    CentralCache(const CentralCache& copy) = delete;
    CentralCache& operator=(const CentralCache& copy) = delete;

    SpanList _spanLists[FREE_LIST_NUM]; //哈希桶中挂的是一个个Span
    static CentralCache _sInst;  // 饿汉式单例模式创建一个CentralCache
};