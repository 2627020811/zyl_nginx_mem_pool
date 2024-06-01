#include "ngx_mem_pool.h"

// 创建指定size大小的内存池，但是小块内存池不超过1个页面的大小

void *ngx_mem_pool::ngx_create_pool(size_t size)
{
    ngx_pool_s *p;

    p = (ngx_pool_s *)malloc(size); 
    if (p == nullptr)
    {
        return nullptr;
    }

    p->d.last = (u_char *)p + sizeof(ngx_pool_s); //初始的前面几个字节用于储存内存池头部信息,所以下一次分配的开始应该前移头部的大小
    p->d.end = (u_char *)p + size; //内存池节点的结尾
    p->d.next = nullptr;//由于当前内存池只有一个节点所以next为NULL
    p->d.failed = 0;

    size = size - sizeof(ngx_pool_s);
    p->max = (size < NGX_MAX_ALLOC_FROM_POOL) ? size : NGX_MAX_ALLOC_FROM_POOL;

    p->current = p; // 自指指针
    p->large = nullptr;
    p->cleanup = nullptr;

    pool = p;
    return p;
}

// 考虑内存字节对齐，从内存池申请size大小的内存
void *ngx_mem_pool::ngx_palloc(size_t size)
{
    if (size <= pool->max) //判断是小块内存 还是大块内存
    {
        return ngx_palloc_small(size, 1);
    }
    return ngx_palloc_large(size);
}

// 和上面的函数一样，不考虑内存字节对齐
void *ngx_mem_pool::ngx_pnalloc(size_t size)
{
    if (size <= pool->max)
    {
        return ngx_palloc_small(size, 0);
    }
    return ngx_palloc_large(size);
}

// 调用的是ngx_palloc实现内存分配，但是会初始化
void *ngx_mem_pool::ngx_pcalloc(size_t size)
{
    void *p;
    p = ngx_palloc(size);
    if (p)
    {
        ngx_memzero(p, size);
    }
    return p;
}

// 释放大块内存
void ngx_mem_pool::ngx_pfree(void *p)
{
    ngx_pool_large_s *l;

    for (l = pool->large; l; l = l->next)
    {
        if (p == l->alloc)
        {
            free(l->alloc);
            l->alloc = nullptr;
            return;
        }
    }
}

// 内存重置函数
void ngx_mem_pool::ngx_reset_pool()
{
    ngx_pool_s *p;
    ngx_pool_large_s *l;

    for (l = pool->large; l; l = l->next)
    {
        if (l->alloc)
        {
            free(l->alloc);
        }
    }

    p = pool;
    p->d.last = (u_char *)p + sizeof(ngx_pool_s);
    p->d.failed = 0;

    for (p = p->d.next; p; p = p->d.next)
    {
        p->d.last = (u_char *)p + sizeof(ngx_pool_data_t);
        p->d.failed = 0;
    }

    pool->current = pool;
    pool->large = nullptr;
}

// 内存池的销毁函数
void ngx_mem_pool::ngx_destroy_pool()
{
    ngx_pool_s *p, *n;
    ngx_pool_large_s *l;
    ngx_pool_cleanup_s *c;

    for (c = pool->cleanup; c; c = c->next)
    {
        if (c->handler)
        {
            c->handler(c->data); //调用需要在内存池释放时同步调用的方法
        }
    }

    for (l = pool->large; l; l = l->next) //释放大块内存
    {
        if (l->alloc)
        {
            free(l->alloc);
        }
    }

    for (p = pool, n = pool->d.next; /*void*/; p = n, n = n->d.next) //销毁内存池节点
    {
        free(p);
        if (n == nullptr)
        {
            break;
        }
    }
}

// 添加回调清理操作的函数
ngx_pool_cleanup_s *ngx_mem_pool::ngx_pool_cleanup_add(size_t size)
{
    ngx_pool_cleanup_s *c;

    c = (ngx_pool_cleanup_s *)ngx_palloc(sizeof(ngx_pool_cleanup_s));
    if (c == nullptr)
    {
        return nullptr;
    }

    if (size)
    {
        c->data = ngx_palloc(size);
        if (c->data == nullptr)
        {
            return nullptr;
        }
    }
    else
    {
        c->data = nullptr;
    }
    // 链表头插
    c->handler = nullptr;
    c->next = pool->cleanup;
    pool->cleanup = c;

    return c;
}

// 小块内存分配
void *ngx_mem_pool::ngx_palloc_small(size_t size, ngx_uint_t align)
{
    u_char *m;
    ngx_pool_s *p;
    p = pool->current;

    do  ////尝试在已有的内存池节点中分配内存
    {
        m = p->d.last;

        if (align)
        {
            m = ngx_align_ptr(m, NGX_ALIGNMENT); // 对齐指针
        }

        if ((ssize_t)(p->d.end - m) >= size)
        {
            p->d.last = m + size;
            return m;
        }
        p = p->d.next;
    } while (p);

    return ngx_palloc_block(size); //当前已有节点都分配失败，创建一个新的内存池节点
}

// 大块内存分配
void *ngx_mem_pool::ngx_palloc_large(size_t size)
{
    void *p;
    ngx_uint_t n;
    ngx_pool_large_s *large;

    p = malloc(size);
    if (p == nullptr)
    {
        return nullptr;
    }

    n = 0;
    for (large = pool->large; large; large = large->next)
    {
        if (large->alloc == nullptr)
        {
            large->alloc = p;
            return p;
        }
        if (n++ > 3)
        {
            break;
        }
    }

    large = (ngx_pool_large_s *)ngx_palloc_small(sizeof(ngx_pool_large_s), 1);
    if (large == nullptr)
    {
        free(p);
        return nullptr;
    }
    // 头插法
    large->alloc = p;
    large->next = pool->large;
    pool->large = large;

    return p;
}

// 创建新的小块内存池节点
void *ngx_mem_pool::ngx_palloc_block(size_t size)
{
    u_char *m;
    size_t psize;
    ngx_pool_s *p, *newpool;

    psize = (size_t)(pool->d.end - (u_char *)pool);

    m = (u_char *)malloc(psize);
    if (m == nullptr)
    {
        return nullptr;
    }
    //(这里有个需要注意的地方，当当前内存节点的剩余空间不够分配时，nginx会重新创建一个ngx_pool_t对象，并且将pool.d->next指向新的ngx_pool_t(ngx_pool_s),新分配的ngx_pool_t对象只用到了ngx_pool_data_t区域，并没有头部信息，头部信息部分已经被当做内存分配区域了)
    newpool = (ngx_pool_s *)m;

    newpool->d.end = m + psize;
    newpool->d.next = nullptr;
    newpool->d.failed = 0;

    m += sizeof(ngx_pool_data_t);
    m = ngx_align_ptr(m, NGX_ALIGNMENT);
    newpool->d.last = m + size;

    for (p = pool->current; p->d.next; p = p->d.next)
    {
        if (p->d.failed++ > 4) //如果内存池当前节点的分配失败次数已经大于等于6次（p->d.failed++ > 4）,则将当前内存池节点前移一个
        {
            pool->current = p->d.next;
        }
    }
    p->d.next = newpool;
    return m;
}