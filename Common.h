#pragma once

#include <iostream>
#include <vector>
#include <cassert>
#include <thread>
#include <mutex>
#include <unordered_map>
#include "ObjectPool.h"

using std::vector;
using std::cout;
using std::endl;

static const size_t FREE_LIST_NUM = 208;    // 哈希表中自由链表个数
static const size_t MAX_BYTES = 256 * 1024; // ThreadCache单次申请的最大字节数
static const size_t PAGE_NUM = 129;     // span的最大管理页数
static const size_t PAGE_SHIFT = 13;    // 一页多少位，这里给一页8KB，13位
typedef size_t PageID;



// #ifdef _WIN32
//     #include <Windows.h>    // Windows下的头文件
// #else
//             // Linux相关的头文件 
// #endif  // _WIN32

// inline static void* SystemAlloc(size_t kpage)
// {
// #ifdef _WIN32   // Windows下的系统调用接口
//     void* ptr = VirtualAlloc(0, kpage << PAGE_SHIFT, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
// #else
//     // Linux下的brk mmap等
// #endif
    
//     if (ptr == nullptr)
//     {
//         throw std::bad_alloc();
//     }

//     return ptr;
// }

#ifdef _WIN32
    #include <Windows.h>    // Windows下的头文件
#else
    #include <sys/mman.h>        // Linux/maxOS内存映射头文件
#endif  // _WIN32

inline static void* SystemAlloc(size_t kpage)
{
    void* ptr = nullptr;

#ifdef _WIN32   // Windows下的系统调用接口
    ptr = VirtualAlloc(0, kpage << PAGE_SHIFT, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
#else           // Linux/MacOS系统调用
    ptr = mmap(nullptr, kpage << PAGE_SHIFT, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (ptr == MAP_FAILED)      // mmap失败
    {
        ptr = nullptr;
    }
#endif
    
    if (ptr == nullptr)
    {
        throw std::bad_alloc();
    }

    return ptr;
}

inline static void SystemFree(void* ptr, size_t size)
{
#ifdef _WIN32
    VirtualFree(ptr, 0, MEM_RELEASE);
#else
    if (ptr != nullptr && ptr != MAP_FAILED)
    {
        munmap(ptr, size);
    }

#endif
}

/* ObjNext如果没有引用，返回的是一个右值，因为ObjNext返回值是一个拷贝，是一个临时对象，
临时对象具有常属性，不能被修改，即是一个右值，右值无法进行赋值操作 */
static void*& ObjNext(void* obj)    // obj的头4/8个字节
{   // 函数用static修饰，防止多个.cpp文件重复包含该Common头文件导致链接时产生冲突
    return *(void**)obj;
}

// ThreadCache中的自由链表
class FreeList
{
public:
    // 删除桶中n个块（头删法），并把删除的空间作为输出型参数返回
    void PopRange(void*& start, void*& end, size_t n)
    {
        // 删除块数不能超过size块
        assert(n <= _size);

        start = end = _freeList;

        for (size_t i = 0; i < n - 1; ++i)
        {
            end = ObjNext(end);
        }

        _freeList = ObjNext(end);
        ObjNext(end) = nullptr;
        _size -= n;
    }

    // 获取当前桶中有多少块空间
    size_t Size()
    {
        return _size;
    }

    // 向自由链表中头插，且插入多块空间
    void PushRange(void* start, void* end, size_t size)
    {
        ObjNext(end) = _freeList;
        _freeList = start;

        _size += size;
    }

    bool Empty()    // 判断哈希表是否为空
    {
        return _freeList == nullptr;
    }
    
    void Push(void* obj)    // 回收空间
    {
        // 头插法
        assert(obj);    // 插入非法空间

        // *(void**)obj = _freeList;
        ObjNext(obj) = _freeList;
        _freeList = obj;

        ++_size;    // 插入一块，size + 1
    }

    void* Pop()     // 提供空间
    {   // 头删法
        assert(_freeList);  // 提供空间的前提是要有空间

        void* obj = _freeList;
        _freeList = ObjNext(obj);

        --_size;    // 去掉一块，_size - 1

        return obj;
    }

    // FreeList当前未到上限时，能够申请的最大块空间是多少
    size_t& MaxSize()
    {
        return _maxSize;
    }

private:
    void* _freeList = nullptr;  // 自由链表，初始为空
    /* 当前自由链表申请未达到上限时，能够申请的最大块空间_maxSize
       初始值为1，表示第一次能申请的就是1块
       到了上限之后_maxSize这个值就作废了    
    */
    size_t _maxSize = 1;

    size_t _size = 0;   // 当前自由链表中有多少块空间
};

class SizeClass
{
    // 线程申请size的对齐规则：整体控制在最多10%左右的内碎片浪费
	//	size范围				对齐数				对应哈希桶下标范围
    // [1,128]					8B 对齐      		freelist[0,16)
    // [128+1,1024]			    16B 对齐  			freelist[16,72)
    // [1024+1,8*1024]			128B 对齐  			freelist[72,128)
    // [8*1024+1,64*1024]		1024B 对齐    		freelist[128,184)
    // [64*1024+1,256*1024]	    8192B 对齐  		freelist[184,208)
public:
    // 计算每个分区对应的对齐后的字节数，alignNum是size对应的对齐数
    // static size_t _RoundUp(size_t size, size_t alignNum)
    // {
    //     size_t res = 0;
    //     if (size % alignNum != 0)
    //     {   // 有余数，要多给一个对齐，如size = 3， （3/8 + 1）* 8 = 8
    //         res = (size / alignNum + 1) * alignNum;
    //     }
    //     else
    //     {
    //         res = size;
    //     }
    //     return res;
    // }
    // 二进制写法
    static size_t _RoundUp(size_t size, size_t alignNum)
    {
        return ((size + alignNum - 1) & ~(alignNum - 1));
    }

    static size_t RoundUp(size_t size)  // 计算对齐后的字节数
    {
        if (size <= 128)   
        {   // [1, 128] 8B
            return _RoundUp(size, 8);
        }
        else if (size <= 1024)
        {   // [128+1, 1024]  16B
            return _RoundUp(size, 16);
        }
        else if (size <= 8 * 1024)
        {   // [1024+1, 8*1024]  128B
            return _RoundUp(size, 128);
        }
        else if (size <= 64 * 1024)
        {   // [8*1024+1, 64*1024]  1024B
            return _RoundUp(size, 1024);
        }
        else if (size <= 256 * 1024)
        {   // [64*1024+1, 256*1024]  8192B
            return _RoundUp(size, 8192);
        }
        else 
        {   // 单次申请空间大于256KB，直接按照页来对齐
            return _RoundUp(size, 1 << PAGE_SHIFT);
        }
    }

    // 求size对应在哈希表中的下标
    // align_shift指对齐数的二进制位数，如size=2，对齐数为8=2^3，align_shift=3
    static inline size_t _Index(size_t size, size_t align_shift)
    {
        return ((size + (1 << align_shift) - 1) >> align_shift) - 1;
    }

    // 计算映射的是哪一个自由链表桶
    // _Index计算的是当前size所在区域的第几个下标，所以Index返回值需要加上前面所有哈希桶个数
    static inline size_t Index(size_t size)
    {
        assert(size <= MAX_BYTES);
        
        // 每个区间有多少个链表
        static int group_array[4] = {16, 56, 56, 56};
        if (size <= 128)
        {   // [1, 128] 8B   --> 8=2^3
            return _Index(size, 3);
        }
        else if (size <= 1024)
        {   // [128+1, 1024]  16B   --> 16=2^4
            return _Index(size - 128, 4) + group_array[0];
        }
        else if (size <= 8 * 1024)
        {   // [1024+1, 8*1024]  128B
            return _Index(size - 1024, 7) + group_array[0] + group_array[1];
        }
        else if (size <= 64 * 1024)
        {   // [8*1024+1, 64*1024]  1024B
            return _Index(size - 8 * 1024, 10) + group_array[2] + group_array[1] + group_array[0];
        }
        else if (size <= 256 * 1024)
        {    // [64*1024+1, 256*1024]  8192B
            return _Index(size - 64 * 1024, 13) + group_array[3] + 
            group_array[2] + group_array[1] + group_array[0];
        }
        else
        {
            assert(false);
        }
        return -1;
    }

    static size_t NumMoveSize(size_t size)
    {
        assert(size > 0);   // 不能申请0大小的空间

        // MAX_BYTES即单个块的最大空间，也就是256KB
        int num = MAX_BYTES / size; 

        if (num > 256)
        {  
            /* 比如单次申请的是8B，256KB除以8B得到的是一个三万多的书，这样单次上限三万多块太多了
               直接分配三万多可能会造成很多浪费的空间，不太现实，所以应该调节小一些
            */
            num = 512;
        }

        if (num < 2)
        {
            /* 比如单次申请的是256KB，除之后得1，如果256KB上限一直是1，这样有点太少了，可能线程要的是4个256KB，
               所以将num改成2可以少调用几次，也就少几次开销，但是也不能太多，256KB空间是很大的，num太高了不太现实，可能会出现浪费
            */
            num = 2;
        }

        // [2, 512] 一次批量移动多少个对象的上限值（慢启动）
        // 大对象一次批量上限高
        // 小对象一次批量上限低

        return num;
    }

    // 块页匹配算法（size表示一块的大小
    static size_t NumMovePage(size_t size)
    {/* 当cc中没有span为tc提供小块空间时，cc就需要向pc申请一块span，
        此时需要根据一块空间大小来匹配出一个维护页空间较为合适的span，
        以保证span为size后尽量不浪费或不足够还再频繁申请相同大小的span
        */

        // NumMoveSize是算出tc向cc申请size大小的块时的单次最大申请块数
        size_t num = NumMoveSize(size);

        // num * size就是单次申请最大空间大小
        size_t npage = num * size;

        /*  PAGE_SHIFT表示一页要占用多少位，比如一页8KB就是13位，
            这里右移其实是除以页大小，算出来就是单次申请最大空间有多少页
        */
       npage >>= PAGE_SHIFT;

        /*  如果算出来为0，直接给1页，比如说size为8B时，num就是512，npage算出来就是4KB
            如果一页8KB，算出来直接为0，即半页的空间都够8B的单次申请的最大空间了，但是二进制中没有0.5，所以只能给1页
        */
    //    if (npage == 0) 
    //    {
    //     npage = 1;
    //    }

        return npage == 0 ? 1 : npage;
    }
};

// 以页为基本单位的结构体
struct Span
{
    PageID _pageId = 0;     // 页号
    size_t _n = 0;          // 当前span管理的页的数量
    size_t _objSize = 0;     // span管理页被切分成的块大小

    Span* _next = nullptr;  // 指向前一个节点
    Span* _prev = nullptr;  // 指向后一个节点

    void* _freeList = nullptr;  // 每个span下面挂的小块空间的头结点
    size_t _usecount = 0;   // 当前span分配出去了多少个块空间

    bool _isUse = false;    // 判断当前span是在cc中还是在pc中
};

class SpanList
{
public:
    // 删除掉第一个span
    Span* PopFront()
    {
        // 先获取到_head后面的第一个span
        Span* front = _head->_next;
        // 删除掉这个span，直接复用Erase
        Erase(front);

        // 返回原来的第一个span
        return front;
    }

    // 判空
    bool Empty()
    {// 双向循环空的时候_head指向自己
        return _head == _head->_next;
    }

    // 头插法
    void PushFront(Span* span)
    {
        Insert(Begin(), span);
    }

    // 头结点
    Span* Begin()
    {
        return _head->_next;
    }

    // 尾节点
    Span* End()
    {
        return _head;
    }

    SpanList()
    {// 构造函数中哨兵头结点
        _head = new Span;

        // 双向链表
        _head->_next = _head;
        _head->_prev = _head;
    }

    void Insert(Span* pos, Span* ptr)
    {// 在pos前面插入ptr
        assert(pos);
        assert(ptr);

        Span* prev = pos->_prev;

        prev->_next = ptr;
        ptr->_prev = prev;

        ptr->_next = pos;
        pos->_prev = ptr;
    }

    void Erase(Span* pos)
    {
        assert(pos);
        assert(pos != _head);   // pos不能是哨兵位

        Span* prev = pos->_prev;
        Span* next = pos->_next;

        prev->_next = next;
        next->_prev = prev;

        /* pos节点不需要调用delete删除，因为pos节点的span需要回收利用，而不是直接删掉 */
        // 回收相关逻辑
    }

public:
    std::mutex _mtx;    // 每个CentralCache中的哈希桶都要有一个桶锁
    
private:
    Span* _head;    // 哨兵位头结点
};


