#include<stdio.h>
#include<stdlib.h>
#include<string.h>

#define MEM_PAGE_SIZE 4096 //内存页大小 4k

//内存池结构体
typedef struct mempool_s{
    size_t block_size;//内存块大小
    size_t free_count;//空闲块数量
    void* mem;//内存池起始地址
    void* free_list;//空闲链表头指针，永远指向第一个空闲块

}mempool_t;

mempool_t g_mp;

int memp_init(mempool_t* mp,size_t block_size){
    //1.入参校验
    if(mp==NULL||sizeof(block_size)<sizeof(void*)){
        return -1;
    }
    //2.初始化结构体
    memset(mp,0,sizeof(mempool_t));
    mp->block_size=block_size;
    //3.申请内存
    mp->mem=malloc(MEM_PAGE_SIZE);
    if(mp->mem==NULL){
        return -1;
    }
    mp->free_count=MEM_PAGE_SIZE/block_size;
    //4.初始化链表头，指向第一块的地址
    mp->free_list=mp->mem;

    //5.给每个块存放下一个块的地址，除了最后一块
    char* ptr=mp->free_list;
    for(int i=0;i<mp->block_size-1;i++){
        *(char**)ptr=ptr+block_size;//将下一块的内存地址存放在当前块内存中

        //对下一块进行地址存放
        ptr+=block_size;
    }
    //最后一块的下一个地址设为NULL，代表链表结束
    *(char**)ptr=NULL;

    return 0;
}

void* memp_alloc(mempool_t* mp){
    //入参校验 + 没有空闲块
    if(mp==NULL||mp->block_size<=0||mp->free_list==NULL){
        return NULL;
    }
    //1.取出当前链表的第一个空闲块，作为放回置
    void* ret=mp->free_list;

    //2.链表头被取出后，需要往后移动一格
    //因为每一块的开头都存放下一块的地址
    mp->free_list=*(void**)mp->free_list;

    //3.空闲块数量减1
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
    //再将新插入的块最为链表头
    *(void**)ptr=mp->free_list;
    mp->free_list=ptr;

    //空闲块数量加1
    mp->free_count++;

}

//销毁内存池，释放整块内存页大小
void memp_destroy(mempool_t* mp){
    if(mp==NULL || mp->mem==NULL){
        return ;
    }
    free(mp->mem);
    memset(mp,0,sizeof(mempool_t));
}
// ========== 自定义 malloc/free 封装 ==========
// 注意：宏替换必须写在这下面，不能写在memp_init前面
// 否则memp_init里的系统malloc会被替换成_malloc，造成死循环
void* _malloc(size_t size){
    printf("_malloc 申请 %zu 字节\n",size);
    if(size<=0){
        return NULL;
    }
    //从全局内存池分配
    void* ptr=memp_alloc(&g_mp);
    if(ptr){
        printf("  ✓ 分配成功，地址: %p\n", ptr);
    }
    else{
        printf("  ✗ 内存池已用完，分配失败\n");
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
#define free(size) _free(size)

int main(){
    int ret=memp_init(&g_mp,64);
    if(ret!=0){
        printf("内存池初始化失败！\n");
        return -1;
    }
    printf("=====内存池初始化完成！=====\n");
    printf("总块数: %zu,每块大小: %zu字节\n\n", g_mp.free_count, g_mp.block_size);

    // 2. 测试分配3块内存
    printf("--- 开始分配 ---\n");
    void* p1=malloc(20);
    void* p2=malloc(100);
    void* p3=malloc(50);
    printf("\n");
    //查看剩余空闲块数量
    printf("剩余空闲块数量：%zu\n",g_mp.free_count);
    // 3. 测试释放2块内存
    printf("--- 开始释放 ---\n");
    free(p2);
    free(p3);
        printf("释放2块后，剩余空闲块: %zu\n\n", g_mp.free_count);

    void* p4 = malloc(15);
    printf("p4地址: %p (和p3地址相同，说明内存被复用了)\n", p4);

    // 4. 释放所有内存
    printf("--- 全部释放 ---\n");
    free(p1);
    free(p4);
    printf("全部释放后，剩余空闲块: %zu\n", g_mp.free_count);

    //5.销毁内存池
    memp_destroy(&g_mp);
    printf("\n内存池已销毁，程序结束\n");

    return 0;
}