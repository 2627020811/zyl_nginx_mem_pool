#include "ngx_mem_pool.h"
#include <stdio.h>
#include <string.h>

typedef struct Data stData;
struct Data
{
    char *ptr;
    FILE *pfile;
};

void func1(void *p1)
{
    char *p = (char *)p1;
    printf("free ptr mem!\n");
    free(p);
}

void func2(void *pf1)
{
    FILE *pf = (FILE *)pf1; //FILE 是一个在 C 标准库中定义的类型，用于表示文件流。文件指针通常用于文件操作函数，如 fopen, fclose, fread, fwrite 等
    printf("close file!\n");
    fclose(pf);
}

int main()
{
    // ngx_create_pool =>代码逻辑可以直接实现在mempool构造函数中
    ngx_mem_pool mempool;
    if (nullptr == mempool.ngx_create_pool(512))
    {
        printf("ngx_create_pool fail...\n");
        return -1;
    }

    void *p1 = mempool.ngx_palloc(128);
    if (p1 == nullptr)
    {
        printf("ngx_palloc 128 bytes fail...\n");
        return -1;
    }

    stData *p2 = (stData *)mempool.ngx_palloc(512);
    if (p2 == nullptr)
    {
        printf("ngx_palloc 512 bytes fail...\n");
        return -1;
    }

    p2->ptr = (char *)malloc(12);
    strcpy(p2->ptr, "hello world");
    p2->pfile = fopen("data.txt", "w");

    ngx_pool_cleanup_s *c1 = mempool.ngx_pool_cleanup_add(sizeof(char *));
    c1->handler = func1;
    c1->data = p2->ptr;

    ngx_pool_cleanup_s *c2 = mempool.ngx_pool_cleanup_add(sizeof(char *));
    c2->handler = func2;
    c2->data = p2->pfile;

    // ngx_destroy_pool =>代码逻辑可以直接实现在mempool析构函数中
    mempool.ngx_destroy_pool(); // 1.调用所有的预置的清理函数 2.释放大块内存 3.释放小块内存池所有内存

    return 0;
}