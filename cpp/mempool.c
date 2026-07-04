#include<stdio.h>
#include<stdlib.h>
#include<string.h>

#define MEM_PAGE_SIZE 4096 //内存页大小 4k

//内存池结构体-基于多页自动扩容
typedef struct mempool_s{
    size_t block_size;//内存块大小,单位为字节
    size_t free_count;//空闲块数量
    void* page_list;//内存页链表头指针，串起向系统申请的所有头指针
    void* free_list;//空闲链表头指针，永远指向第一个空闲块,不参与分配空间，只是用来指向第一个空闲块

}mempool_t;

mempool_t g_mp;//全局内存池实例

//内部扩容函数，扩容，申请新的一页内存
static int memp_expand(mempool_t* mp){
    //1.向系统申请新的一页内存
    void* new_page=malloc(MEM_PAGE_SIZE);
    if(new_page==NULL){
        return -1;
    }

    //2.把新页插入到页链表的头部
    *(void**)new_page=mp->page_list;
    //页链表头更新为新页
    mp->page_list=new_page;

    //3.计算这一页能切出多少块
    //注意：页开头要预留sizeof(void*)字节存放页链表指针，不能用来分配
    int usable_size=MEM_PAGE_SIZE-sizeof(void*);
    int block_cnt=usable_size/mp->block_size;
    if(block_cnt<=0){
        free(new_page);
        return -1;
    }

    //4.计算第一个空闲块的起始地址（跳过页头的页链表指针）
    char* block_start=(char*)new_page+sizeof(void*);
    char* ptr=block_start;

    //5.初始化新页的空闲块链表
    for(int i=0;i<block_cnt-1;i++){
        //当前块开头写下一块地址
        *(char**)ptr=ptr+mp->block_size;
        ptr+=mp->block_size;
    }

    //新页的最后一块指向原来的空闲链表头
    *(char**)ptr=mp->free_list;
    //6.更新全局空闲链表头，指向新页的第一块
    mp->free_list=block_start;
    //7.更新总空闲块
    mp->free_count+=block_cnt;
    printf("  [扩容] 新增一页内存，新增 %d 个空闲块，当前总空闲块: %zu\n", block_cnt, mp->free_count);
    return 0;

}

int memp_init(mempool_t* mp,size_t block_size){
    //1.入参校验
    if(mp==NULL||sizeof(block_size)<sizeof(void*)){
        return -1;
    }
    //2.初始化结构体
    memset(mp,0,sizeof(mempool_t));
    mp->block_size=block_size;
    //初始化直接扩容一次，申请第一页内存
    if(memp_expand(mp)!=0){
        return -1;
    }

    return 0;
}

void* memp_alloc(mempool_t* mp){
    //入参校验
    if(mp==NULL){
        return NULL;
    }

    //核心：没有空闲块，自动触发扩容
    if(mp->free_count<=0||mp->free_list==NULL){
        if(memp_expand(mp)!=0){
            return NULL;//扩容失败
        }
    }

    //分配内存块
    //取出链表头的空闲块
    void* ret=mp->free_list;
    //因为每个内存块头部都存有下一内存块地址
    mp->free_list=*(void**)mp->free_list;
    mp->free_count--;
    return ret;

}

//使用头插法将内存归还到内存池
//ptr为要释放的地址
void memp_free(mempool_t* mp,void* ptr){
    //入参校验
    if(mp==NULL || ptr==NULL){
        return ;
    }
    //原本每一个块的开头都存放着下一块的地址，相当于是next指针
    //使用头插法后，当前块的开头则指向链表头的地址
    //再将新插入的块作为链表头
    *(void**)ptr=mp->free_list;
    mp->free_list=ptr;

    //空闲块数量加1
    mp->free_count++;

}

//销毁内存池，释放所有向系统申请的内存页
void memp_destroy(mempool_t* mp){
    if(mp==NULL || mp->page_list==NULL){
        return ;
    }
    
    //遍历链表，逐页释放
    void* current=mp->free_list;
    while(current){
        //取出下一页
        void* next=*(void**)current;
        free(current);//释放当前页
        //跳到下一页
        current=next;
    }

    memset(mp,0,sizeof(mempool_t));
    printf("\n内存池已销毁，所有页内存已释放\n");
}
// ========== 自定义 malloc/free 封装 ==========
// 注意：宏替换必须写在这下面，不能写在memp_init前面
// 否则memp_init里的系统malloc会被替换成_malloc，造成死循环
void* _malloc(size_t size){
    printf("_malloc 申请 %zu 字节\n",size);

    //从全局内存池分配
    void* ptr=memp_alloc(&g_mp);
    if(ptr){
        printf("  ✓ 分配成功，地址: %p\n", ptr);
    }
    else{
        printf("  ✗ 分配失败\n");
    }
    return ptr;
}

void _free(void* ptr){
    printf("_free: 释放地址 %p\n", ptr);
    memp_free(&g_mp,ptr);
    printf("  ✓ 已归还内存池\n");
}
//宏替换
#define malloc(size) _malloc(size)
#define free(ptr) _free(ptr)

// ========== 测试主函数 ==========
int main() {
    // 初始化：每块64字节
    int ret = memp_init(&g_mp, 64);
    if (ret != 0) {
        printf("内存池初始化失败\n");
        return -1;
    }
    printf("=== 内存池初始化完成 ===\n");
    printf("每块大小: %ld字节，初始空闲块: %zu\n\n", g_mp.block_size, g_mp.free_count);

    // 测试：循环分配超过单页容量的块，触发自动扩容
    printf("--- 开始批量分配，触发自动扩容 ---\n");
    #define ALLOC_CNT 80  // 单页只有63块左右，分配80次必然触发扩容
    void* ptrs[ALLOC_CNT];
    for (int i = 0; i < ALLOC_CNT; i++) {
        ptrs[i] = malloc(64);
    }
    printf("\n分配完成，剩余空闲块: %ld\n\n", g_mp.free_count);

    // 测试：批量释放
    printf("--- 批量释放 ---\n");
    for (int i = 0; i < ALLOC_CNT; i++) {
        free(ptrs[i]);
    }
    printf("释放完成，剩余空闲块: %ld\n", g_mp.free_count);

    // 销毁内存池
    memp_destroy(&g_mp);
    return 0;
}