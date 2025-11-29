#pragma once
#include "Common.h"

class PageCache
{
public:
    // 提供公有实例
    static PageCache* GetInstance()
    {
        return &_sInst;
    }

    // pc从_spanLists中拿出来一个k页的span
    Span* NewSpan(size_t k);

    // 通过页地址找到span
    Span* MapObjectToSpan(void* obj);

    // 管理cc归还回来的span
    void ReleaseSpanToPageCache(Span* span);
public:
    std::mutex _pageMtx;    // pc全局的锁
    
private:
    SpanList _spanLists[PAGE_NUM];  // pc中的哈希表

    // 哈希映射，用来快速通过页号找到对应span
    std::unordered_map<PageID, Span*> _idSpanMap;

    // 创建span的对象池
    ObjectPool<Span> _spanPool;

    // 私有化构造函数
    PageCache() {}

    // 删除拷贝构造函数和赋值运算符重载函数
    PageCache(const PageCache& pc) = delete;
    PageCache& operator=(const PageCache& pc) = delete;

    static PageCache _sInst;    // 单例类对象  唯一实例
};