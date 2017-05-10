#include "mempool.h"


#define Zoo_MemPool_Offsetof(TYPE, MEMBER) ((size_t) &((TYPE *)0)->MEMBER)

#define Zoo_MemPool_Container_Of(ptr, type, member) (type *)( (char *)ptr - Zoo_MemPool_Offsetof(type,member) )

typedef struct __ZOO_MM_LIST_HEAD_
{
	struct __ZOO_MM_LIST_HEAD_ *next, *prev;
}ZOO_MM_LIST_HEAD;

#define ZOO_MM_LIST_HEAD_INIT(name) { &(name), &(name) }

#define ZOO_MM_LIST_HEAD_DEFINE(name) \
struct ZOO_MM_LIST_HEAD name = ZOO_MM_LIST_HEAD_INIT(name)

static void Zoo_MemPool_Init_List_Head( ZOO_MM_LIST_HEAD *list)
{
	list->next = list;
	list->prev = list;
}

static void __Zoo_MemPool_List_add( ZOO_MM_LIST_HEAD *new,
					   ZOO_MM_LIST_HEAD *prev,
					   ZOO_MM_LIST_HEAD *next)
{
	next->prev = new;
	new->next = next;
	new->prev = prev;
	prev->next = new;
}

//	从当前位置插入
static void Zoo_MemPool_List_Add( ZOO_MM_LIST_HEAD *new, ZOO_MM_LIST_HEAD *head)
{
	__Zoo_MemPool_List_add(new, head, head->next);
}

static void __list_del( ZOO_MM_LIST_HEAD * prev, ZOO_MM_LIST_HEAD * next)
{
	next->prev = prev;
	prev->next = next;
}

//	删除当前个
static void Zoo_MemPool_List_Del( ZOO_MM_LIST_HEAD *entry)
{
	__list_del(entry->prev, entry->next);
	entry->next = 0;
	entry->prev = 0;
}

//	判断当前链表是否为空
static int Zoo_MemPool_List_Is_Empty(const ZOO_MM_LIST_HEAD *head)
{
	return head->next == head;
}

//	获取结构体首地址
#define zoo_mm_list_entry(ptr, type, member) \
	Zoo_MemPool_Container_Of(ptr, type, member)

//	循环遍历整个链表
#define zoo_mm_list_for_each(pos, head) \
	for (pos = (head)->next; pos != (head); pos = pos->next)




typedef struct _ZOO_MEMPOOL_STRUCT 
{
	ZOO_MM_LIST_HEAD poolHead;
	unsigned int poolAllocLength;
	ZOO_MEMPOOL_FUNCTION zmf;
}ZOO_MEMPOOL_STRUCT, *PZOO_MEMPOOL_STRUCT;

//static list_head g_mempoolmemory ;		//	池向系统申请的内存块链表
						//		池每次向系统申请一块内存之后，都会把内存的信息挂在这里

#define DEFAULT_MEMORY_LENGTH	0xFFFFFFFF	//	空间如由外部传入，则这里不限大小
								//512
								//( 1024 * 1024 * 10 )	//10M

typedef struct				//	空闲内存空间列表
{
	ZOO_MM_LIST_HEAD point;
	unsigned int length;	//	当前空闲内存块长度
	unsigned char start[0];	//	当前空闲内存块起始地址
} FREE_MEMORY_POOL;

typedef struct				//	向系统申请内存块所用的结构，每个结构为一个池
							//		每个池有自己对应的空闲内存链表表头
{
	ZOO_MM_LIST_HEAD point;
	ZOO_MM_LIST_HEAD head;				//	当前池分配给程序的内存块链表
										//		池每次分配内存给应用程序之后，这里记录没有被分配的内存
	unsigned length;		//	当前内存的整体长度
	unsigned char buf[0];	//	当前内存的起始地址
} SYSTEM_MEMORY_POOL;

static void * __memmove ( void * dst, const void * src, size_t count )
{
	void * ret = dst;
	if (dst <= src || (char *)dst >= ((char *)src + count))
	{
		while (count--)
		{
			*(char *)dst = *(char *)src;
			dst = (char *)dst + 1;
			src = (char *)src + 1;
		}
	}
	else
	{
		dst = (char *)dst + count - 1;
		src = (char *)src + count - 1;
		while (count--)
		{
			*(char *)dst = *(char *)src;
			dst = (char *)dst - 1;
			src = (char *)src - 1;
		}
	}
	return(ret);
}



//
//	函数名：
//		Zoo_MemPool_Init
//	函数功能：
//		初始化内存池
//	参数：
//		length		初始化内存池长度
//	返回值：
//		正确执行返回0
//		错误之行返回非0
//
int Zoo_MemPool_Init(PVOID *p, unsigned int length, PZOO_MEMPOOL_FUNCTION pzmf)
{
	PZOO_MEMPOOL_STRUCT pZms = NULL;
	if (p == NULL)
	{
		return -1;
	}
	if (pzmf == NULL)
	{
		return -1;
	}
	if (pzmf->AllocMemory == NULL)
	{
		return -1;
	}
	if (pzmf->FreeMemory == NULL)
	{
		return -1;
	}
	if (length == 0)
	{
		length = 1024 * 1024;
	}
	pZms = pzmf->AllocMemory(sizeof(*pZms));
	if (pzmf == NULL)
	{
		return -1;
	}
	pZms->zmf = *pzmf;
	pZms->poolAllocLength = length;

	Zoo_MemPool_Init_List_Head( &pZms->poolHead );

	*p = pZms;
	return 0;
}

//
//	函数功能
//		插入一块内存作为池的空间，这块内存由外部来保证可用
//	参数：
//		buf			要插入的内存缓冲区指针
//		length		要插入的内存长度
//	返回值：
//		正确执行返回0
//		错误执行返回非0
//
//	备注：
//		这个接口最初是给外部使用的，
//		这样的话，就可以由外部直接控制池内的内存了，
//		但是后来由于需要内部支持内存申请，所以索性这里就不给外部使用了
//
int Zoo_MemPool_Insert(PVOID p, char *buf , unsigned int length )
{
	SYSTEM_MEMORY_POOL *pSysMem;
	FREE_MEMORY_POOL *pFreeMem;
	PZOO_MEMPOOL_STRUCT pZms = (PZOO_MEMPOOL_STRUCT)p;
	if (pZms == NULL)
	{
		return -1;
	}
	//	插入内存地址为NULL
	if (buf == NULL)
	{
		return -1;
	}
	//	插入内存长度太小，连结构体都放不下
	if (length <= ( sizeof(SYSTEM_MEMORY_POOL) + sizeof(FREE_MEMORY_POOL) ))
	{
	}
	pSysMem = (SYSTEM_MEMORY_POOL *)buf;
	pSysMem->length = length - sizeof(SYSTEM_MEMORY_POOL) ;
	Zoo_MemPool_List_Add( &pSysMem->point , &pZms->poolHead );
	Zoo_MemPool_Init_List_Head( &pSysMem->head );
	pFreeMem = (FREE_MEMORY_POOL *)pSysMem->buf;
	pFreeMem->length = length - sizeof(SYSTEM_MEMORY_POOL) - sizeof(FREE_MEMORY_POOL);
	Zoo_MemPool_List_Add( &pFreeMem->point , &pSysMem->head );
	return 0;
}

//
//	函数名：
//		Zoo_MemPool_Destory
//	函数功能：
//		释放内存池
//	参数：
//		无
//	返回值：
//		正确执行返回0
//		错误之行返回非0
//
int Zoo_MemPool_Destory(PVOID *p)
{
	ZOO_MM_LIST_HEAD *tmp;
	ZOO_MM_LIST_HEAD tmph;
	SYSTEM_MEMORY_POOL *tmps;
	PZOO_MEMPOOL_STRUCT pZms = (PZOO_MEMPOOL_STRUCT)(*p);
	if (pZms == NULL)
	{
		return -1;
	}
	zoo_mm_list_for_each( tmp , &pZms->poolHead )
	{
		tmph = *tmp;
		Zoo_MemPool_List_Del( tmp );
		tmps = Zoo_MemPool_Container_Of( tmp , SYSTEM_MEMORY_POOL , point );
		pZms->zmf.FreeMemory(tmps);
		tmp = &tmph;
	}
	pZms->zmf.FreeMemory(pZms);
	*p = NULL;
	return 0;
}

//
//	函数名：
//		Zoo_MemPool_Malloc
//	函数功能：
//		向池申请内存
//	参数：
//		unSize		要申请的内存长度
//	返回值
//		正确执行返回非0
//		错误执行返回0
//
void * Zoo_MemPool_Malloc(PVOID p, unsigned int unSize )
{
	int mallocBufLen ;
	SYSTEM_MEMORY_POOL *pSysMem;
	ZOO_MM_LIST_HEAD *pSysList;
	FREE_MEMORY_POOL *pFreeMem;
	ZOO_MM_LIST_HEAD *pFreeList;
	FREE_MEMORY_POOL *pFreeLittleMem = 0;
	void *pvRet;
	PZOO_MEMPOOL_STRUCT pZms = (PZOO_MEMPOOL_STRUCT)p;
	if (pZms == NULL)
	{
		return NULL;
	}
	unSize += sizeof(int);
	if ( unSize < sizeof( FREE_MEMORY_POOL ) )
	{
		unSize = sizeof( FREE_MEMORY_POOL );
	}
	unSize = ( unSize + 3 ) & ~3;
	mallocBufLen = unSize + sizeof( FREE_MEMORY_POOL );
	//	如果一次申请的内存长度就超标了
	if ( mallocBufLen >= DEFAULT_MEMORY_LENGTH )
	{
		return 0;
	}
	//	如果长度没有超标，但是内存池是空的
	if ( Zoo_MemPool_List_Is_Empty( &pZms->poolHead ) )
	{
		//	那就申请一块内存插进去
		PVOID pMem;
		if (pZms->poolAllocLength < unSize)
		{
			pZms->poolAllocLength = unSize * 2;
		}
		pMem = pZms->zmf.AllocMemory(pZms->poolAllocLength);
		if (pMem == NULL)
		{
			//	失败了有可能是内存太大了，申请不出来，减半再申请
			pZms->poolAllocLength /= 2;
			pMem = pZms->zmf.AllocMemory(pZms->poolAllocLength);
			if (pMem == NULL)
			{
				return NULL;
			}
			Zoo_MemPool_Insert(p, pMem , pZms->poolAllocLength );
			//	最后内存不用扩容
		}
		else
		{
			Zoo_MemPool_Insert(p, pMem , pZms->poolAllocLength );
			//	最后内存扩容
			pZms->poolAllocLength *= 2;
		}
		//return 0;
	}
	//	如果长度没有超标
	zoo_mm_list_for_each( pSysList , &pZms->poolHead )
	{
		pSysMem = zoo_mm_list_entry( pSysList , SYSTEM_MEMORY_POOL , point );
		zoo_mm_list_for_each( pFreeList , &pSysMem->head )
		{
			pFreeMem = zoo_mm_list_entry( pFreeList , FREE_MEMORY_POOL , point );
			if ( pFreeMem->length >= unSize )
			{
#if 0
				if ( pFreeLittleMem == 0 || pFreeLittleMem->length < pFreeMem->length )
				{	//	替换成最小，最合适的
					pFreeLittleMem = pFreeMem;
				}
#endif
				if ( pFreeLittleMem == 0 || pFreeLittleMem->length < pFreeMem->length )
				{	//	替换成最小，最合适的
					pFreeLittleMem = pFreeMem;
				}
			}
		}
	}
	//	没有内存可用
	if ( 0 == pFreeLittleMem )
	{
		//	那就申请一块内存插进去，让它有内存可用
		PVOID pMem;
		if (pZms->poolAllocLength < unSize)
		{
			pZms->poolAllocLength = unSize * 2;
		}
		pMem = pZms->zmf.AllocMemory(pZms->poolAllocLength);
		if (pMem == NULL)
		{
			//	失败了有可能是内存太大了，申请不出来，减半再申请
			pZms->poolAllocLength /= 2;
			pMem = pZms->zmf.AllocMemory(pZms->poolAllocLength);
			if (pMem == NULL)
			{
				return NULL;
			}
			Zoo_MemPool_Insert(p, pMem , pZms->poolAllocLength );
			//	最后内存不用扩容
		}
		else
		{
			Zoo_MemPool_Insert(p, pMem , pZms->poolAllocLength );
			//	最后内存扩容
			pZms->poolAllocLength *= 2;
		}
		//	最后重入一下，这样用着方便
		return Zoo_MemPool_Malloc(p, unSize);
	}
	pFreeLittleMem->length -= unSize;
	pvRet = pFreeLittleMem->start + pFreeLittleMem->length;

	*(unsigned int*)pvRet = unSize;
	return (void *)((int*)pvRet + 1);
}

//
//	函数名：
//		Zoo_MemPool_Free
//	函数功能：
//		把从池申请的内存释放回池中
//	参数：
//		buffer		从池中获取的内存首地址
//	返回值：
//		正确执行返回0
//		错误之行返回非0
//
int Zoo_MemPool_Free(PVOID p, void *buffer )
{
	SYSTEM_MEMORY_POOL *pSysMem;
	ZOO_MM_LIST_HEAD *pSysList;
	FREE_MEMORY_POOL *pFreeMem;
	ZOO_MM_LIST_HEAD *pFreeList;

	FREE_MEMORY_POOL *pFreePreMem = 0;
	FREE_MEMORY_POOL *pFreePostMem = 0;

	int buflen ;
	PZOO_MEMPOOL_STRUCT pZms = (PZOO_MEMPOOL_STRUCT)p;
	if (pZms == NULL)
	{
		return -1;
	}
	buffer = (void *)((int*)buffer - 1);
	buflen = *(int *)buffer;
	zoo_mm_list_for_each( pSysList , &pZms->poolHead )
	{
		pSysMem = zoo_mm_list_entry( pSysList , SYSTEM_MEMORY_POOL , point );
		if ( ( unsigned char * )buffer < pSysMem->buf )
		{	//	前部分越界了
			continue;
		}
		if ( buflen + (char*)buffer > pSysMem->buf + pSysMem->length )
		{	//	后部分越界了
			continue;
		}
		zoo_mm_list_for_each( pFreeList , &pSysMem->head )
		{
			pFreeMem = zoo_mm_list_entry( pFreeList , FREE_MEMORY_POOL , point );
			if ( pFreeMem->start <= ( unsigned char * )buffer )
			{
				//	当 pre == 0 或者 已经记录的 pre 比当前找到的位置还靠前，也就是距离 buff 更远，就替换值
				if ( 0 == pFreePreMem || pFreePreMem->start < pFreeMem->start )
				{
					pFreePreMem = pFreeMem;
				}
			}
			else
			{
				//	当 post == 0 或者已经记录的post 比当前找到的位置还要靠后，也就是距离buffer更远，就替换
				if ( 0 == pFreePostMem || pFreePostMem->start > pFreeMem->start )
				{
					pFreePostMem = pFreeMem;
				}
			}
		}
		if ( 0 != pFreePreMem || 0 != pFreePostMem )
		{
			break;
		}
	}

	//	如果 pre == 0  post == 0	当前内存不在任意一个内存块当中
	//	如果 pre == 0  post != 0	说明当前内存块是第一个内存块
	//	如果 pre != 0  post == 0	说明当前内存块是最后一块有效内存块
	//	如果 pre != 0  post != 0	说明是两块内存中间的一块

	//	到这里	pSysMem	就是当前内存块，内部保存了要用到的子链表头节点

	//	不在内存块中
	if ( 0 == pFreePreMem && 0 == pFreePostMem )
	{
		return 1;
	}

	//	这是第一个块
	while ( 0 == pFreePreMem && 0 != pFreePostMem )
	{
		//	如果与下一个空闲块中间不连接
		if ( (char*)buffer + buflen < (char *)pFreePostMem )
		{
			pFreePostMem = (FREE_MEMORY_POOL *)buffer;
			pFreePostMem->length = buflen - sizeof( FREE_MEMORY_POOL );
			Zoo_MemPool_List_Add( &pFreePostMem->point , &pSysMem->head );
			break;
		}
		//	如果与下一个空闲块正好对接
		if ( (char*)buffer + buflen == (char *)pFreePostMem )
		{
			Zoo_MemPool_List_Del( &pFreePostMem->point );
			__memmove( buffer , ( (char *)( pFreePostMem->start ) - sizeof( FREE_MEMORY_POOL ) ) , sizeof(FREE_MEMORY_POOL) );
			pFreePostMem = (FREE_MEMORY_POOL *)buffer;
			Zoo_MemPool_List_Add( &pFreePostMem->point , &pSysMem->head );
			pFreePostMem->length += buflen;
			break;
		}
		//	不该出现的错误，自己在下一块内存块的后面
		//__asm int 3;
	}

	//	这是最后一个块
	while ( 0 != pFreePreMem && 0 == pFreePostMem )
	{
		//	如果与上一个空闲块中间没有连接
		if ( pFreePreMem->start + pFreePreMem->length < (char *)buffer )
		{
			pFreePreMem = (FREE_MEMORY_POOL *)buffer;
			pFreePreMem->length = buflen - sizeof( FREE_MEMORY_POOL );
			Zoo_MemPool_List_Add( &pFreePreMem->point , &pSysMem->head );
			break;
		}
		//	如果与上一个空闲块正好对接
		if ( pFreePreMem->start + pFreePreMem->length == (char *)buffer )
		{
			pFreePreMem->length += buflen;
			break;
		}
		//	不该出现的错误 ，自己在上一个模块的前面
		//__asm int 3;
	}

	//	是两块内存中间的一块空间
	while ( 0 != pFreePreMem && 0 != pFreePostMem )
	{
		//	如果与上一个空闲块正好对接
		if ( pFreePreMem->start + pFreePreMem->length == (char *)buffer )
		{
			pFreePreMem->length += buflen;
			//	如果与下一个空闲块正好对接
			if ( ( (char*)buffer + buflen ) == (char *)pFreePostMem )
			{
				Zoo_MemPool_List_Del( &pFreePostMem->point );
				pFreePreMem->length += pFreePostMem->length + sizeof( FREE_MEMORY_POOL );

				//	前后都可以对接，把可以整合的内存块合并到一起，然后
				break;
			}
		}
		//	如果与下一个空闲块正好对接
		if ( (char*)buffer + buflen == ( (char *)( pFreePostMem->start ) - sizeof( FREE_MEMORY_POOL ) ) )
		{
			Zoo_MemPool_List_Del( &pFreePostMem->point );
			__memmove( buffer , ( (char *)( pFreePostMem->start ) - sizeof( FREE_MEMORY_POOL ) ) , sizeof(FREE_MEMORY_POOL) );
			pFreePostMem = (FREE_MEMORY_POOL *)buffer;
			Zoo_MemPool_List_Add( &pFreePostMem->point , &pSysMem->head );
			pFreePostMem->length += buflen;
			break;
		}

		//	没有对接
		pFreePostMem = (FREE_MEMORY_POOL *)buffer;
		pFreePostMem->length = buflen - sizeof( FREE_MEMORY_POOL );
		Zoo_MemPool_List_Add( &pFreePostMem->point , &pSysMem->head );

		break;
	}

	return 0;
}