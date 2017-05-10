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

//	�ӵ�ǰλ�ò���
static void Zoo_MemPool_List_Add( ZOO_MM_LIST_HEAD *new, ZOO_MM_LIST_HEAD *head)
{
	__Zoo_MemPool_List_add(new, head, head->next);
}

static void __list_del( ZOO_MM_LIST_HEAD * prev, ZOO_MM_LIST_HEAD * next)
{
	next->prev = prev;
	prev->next = next;
}

//	ɾ����ǰ��
static void Zoo_MemPool_List_Del( ZOO_MM_LIST_HEAD *entry)
{
	__list_del(entry->prev, entry->next);
	entry->next = 0;
	entry->prev = 0;
}

//	�жϵ�ǰ�����Ƿ�Ϊ��
static int Zoo_MemPool_List_Is_Empty(const ZOO_MM_LIST_HEAD *head)
{
	return head->next == head;
}

//	��ȡ�ṹ���׵�ַ
#define zoo_mm_list_entry(ptr, type, member) \
	Zoo_MemPool_Container_Of(ptr, type, member)

//	ѭ��������������
#define zoo_mm_list_for_each(pos, head) \
	for (pos = (head)->next; pos != (head); pos = pos->next)




typedef struct _ZOO_MEMPOOL_STRUCT 
{
	ZOO_MM_LIST_HEAD poolHead;
	unsigned int poolAllocLength;
	ZOO_MEMPOOL_FUNCTION zmf;
}ZOO_MEMPOOL_STRUCT, *PZOO_MEMPOOL_STRUCT;

//static list_head g_mempoolmemory ;		//	����ϵͳ������ڴ������
						//		��ÿ����ϵͳ����һ���ڴ�֮�󣬶�����ڴ����Ϣ��������

#define DEFAULT_MEMORY_LENGTH	0xFFFFFFFF	//	�ռ������ⲿ���룬�����ﲻ�޴�С
								//512
								//( 1024 * 1024 * 10 )	//10M

typedef struct				//	�����ڴ�ռ��б�
{
	ZOO_MM_LIST_HEAD point;
	unsigned int length;	//	��ǰ�����ڴ�鳤��
	unsigned char start[0];	//	��ǰ�����ڴ����ʼ��ַ
} FREE_MEMORY_POOL;

typedef struct				//	��ϵͳ�����ڴ�����õĽṹ��ÿ���ṹΪһ����
							//		ÿ�������Լ���Ӧ�Ŀ����ڴ������ͷ
{
	ZOO_MM_LIST_HEAD point;
	ZOO_MM_LIST_HEAD head;				//	��ǰ�ط����������ڴ������
										//		��ÿ�η����ڴ��Ӧ�ó���֮�������¼û�б�������ڴ�
	unsigned length;		//	��ǰ�ڴ�����峤��
	unsigned char buf[0];	//	��ǰ�ڴ����ʼ��ַ
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
//	��������
//		Zoo_MemPool_Init
//	�������ܣ�
//		��ʼ���ڴ��
//	������
//		length		��ʼ���ڴ�س���
//	����ֵ��
//		��ȷִ�з���0
//		����֮�з��ط�0
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
//	��������
//		����һ���ڴ���Ϊ�صĿռ䣬����ڴ����ⲿ����֤����
//	������
//		buf			Ҫ������ڴ滺����ָ��
//		length		Ҫ������ڴ泤��
//	����ֵ��
//		��ȷִ�з���0
//		����ִ�з��ط�0
//
//	��ע��
//		����ӿ�����Ǹ��ⲿʹ�õģ�
//		�����Ļ����Ϳ������ⲿֱ�ӿ��Ƴ��ڵ��ڴ��ˣ�
//		���Ǻ���������Ҫ�ڲ�֧���ڴ����룬������������Ͳ����ⲿʹ����
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
	//	�����ڴ��ַΪNULL
	if (buf == NULL)
	{
		return -1;
	}
	//	�����ڴ泤��̫С�����ṹ�嶼�Ų���
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
//	��������
//		Zoo_MemPool_Destory
//	�������ܣ�
//		�ͷ��ڴ��
//	������
//		��
//	����ֵ��
//		��ȷִ�з���0
//		����֮�з��ط�0
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
//	��������
//		Zoo_MemPool_Malloc
//	�������ܣ�
//		��������ڴ�
//	������
//		unSize		Ҫ������ڴ泤��
//	����ֵ
//		��ȷִ�з��ط�0
//		����ִ�з���0
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
	//	���һ��������ڴ泤�Ⱦͳ�����
	if ( mallocBufLen >= DEFAULT_MEMORY_LENGTH )
	{
		return 0;
	}
	//	�������û�г��꣬�����ڴ���ǿյ�
	if ( Zoo_MemPool_List_Is_Empty( &pZms->poolHead ) )
	{
		//	�Ǿ�����һ���ڴ���ȥ
		PVOID pMem;
		if (pZms->poolAllocLength < unSize)
		{
			pZms->poolAllocLength = unSize * 2;
		}
		pMem = pZms->zmf.AllocMemory(pZms->poolAllocLength);
		if (pMem == NULL)
		{
			//	ʧ�����п������ڴ�̫���ˣ����벻����������������
			pZms->poolAllocLength /= 2;
			pMem = pZms->zmf.AllocMemory(pZms->poolAllocLength);
			if (pMem == NULL)
			{
				return NULL;
			}
			Zoo_MemPool_Insert(p, pMem , pZms->poolAllocLength );
			//	����ڴ治������
		}
		else
		{
			Zoo_MemPool_Insert(p, pMem , pZms->poolAllocLength );
			//	����ڴ�����
			pZms->poolAllocLength *= 2;
		}
		//return 0;
	}
	//	�������û�г���
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
				{	//	�滻����С������ʵ�
					pFreeLittleMem = pFreeMem;
				}
#endif
				if ( pFreeLittleMem == 0 || pFreeLittleMem->length < pFreeMem->length )
				{	//	�滻����С������ʵ�
					pFreeLittleMem = pFreeMem;
				}
			}
		}
	}
	//	û���ڴ����
	if ( 0 == pFreeLittleMem )
	{
		//	�Ǿ�����һ���ڴ���ȥ���������ڴ����
		PVOID pMem;
		if (pZms->poolAllocLength < unSize)
		{
			pZms->poolAllocLength = unSize * 2;
		}
		pMem = pZms->zmf.AllocMemory(pZms->poolAllocLength);
		if (pMem == NULL)
		{
			//	ʧ�����п������ڴ�̫���ˣ����벻����������������
			pZms->poolAllocLength /= 2;
			pMem = pZms->zmf.AllocMemory(pZms->poolAllocLength);
			if (pMem == NULL)
			{
				return NULL;
			}
			Zoo_MemPool_Insert(p, pMem , pZms->poolAllocLength );
			//	����ڴ治������
		}
		else
		{
			Zoo_MemPool_Insert(p, pMem , pZms->poolAllocLength );
			//	����ڴ�����
			pZms->poolAllocLength *= 2;
		}
		//	�������һ�£��������ŷ���
		return Zoo_MemPool_Malloc(p, unSize);
	}
	pFreeLittleMem->length -= unSize;
	pvRet = pFreeLittleMem->start + pFreeLittleMem->length;

	*(unsigned int*)pvRet = unSize;
	return (void *)((int*)pvRet + 1);
}

//
//	��������
//		Zoo_MemPool_Free
//	�������ܣ�
//		�Ѵӳ�������ڴ��ͷŻس���
//	������
//		buffer		�ӳ��л�ȡ���ڴ��׵�ַ
//	����ֵ��
//		��ȷִ�з���0
//		����֮�з��ط�0
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
		{	//	ǰ����Խ����
			continue;
		}
		if ( buflen + (char*)buffer > pSysMem->buf + pSysMem->length )
		{	//	�󲿷�Խ����
			continue;
		}
		zoo_mm_list_for_each( pFreeList , &pSysMem->head )
		{
			pFreeMem = zoo_mm_list_entry( pFreeList , FREE_MEMORY_POOL , point );
			if ( pFreeMem->start <= ( unsigned char * )buffer )
			{
				//	�� pre == 0 ���� �Ѿ���¼�� pre �ȵ�ǰ�ҵ���λ�û���ǰ��Ҳ���Ǿ��� buff ��Զ�����滻ֵ
				if ( 0 == pFreePreMem || pFreePreMem->start < pFreeMem->start )
				{
					pFreePreMem = pFreeMem;
				}
			}
			else
			{
				//	�� post == 0 �����Ѿ���¼��post �ȵ�ǰ�ҵ���λ�û�Ҫ����Ҳ���Ǿ���buffer��Զ�����滻
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

	//	��� pre == 0  post == 0	��ǰ�ڴ治������һ���ڴ�鵱��
	//	��� pre == 0  post != 0	˵����ǰ�ڴ���ǵ�һ���ڴ��
	//	��� pre != 0  post == 0	˵����ǰ�ڴ�������һ����Ч�ڴ��
	//	��� pre != 0  post != 0	˵���������ڴ��м��һ��

	//	������	pSysMem	���ǵ�ǰ�ڴ�飬�ڲ�������Ҫ�õ���������ͷ�ڵ�

	//	�����ڴ����
	if ( 0 == pFreePreMem && 0 == pFreePostMem )
	{
		return 1;
	}

	//	���ǵ�һ����
	while ( 0 == pFreePreMem && 0 != pFreePostMem )
	{
		//	�������һ�����п��м䲻����
		if ( (char*)buffer + buflen < (char *)pFreePostMem )
		{
			pFreePostMem = (FREE_MEMORY_POOL *)buffer;
			pFreePostMem->length = buflen - sizeof( FREE_MEMORY_POOL );
			Zoo_MemPool_List_Add( &pFreePostMem->point , &pSysMem->head );
			break;
		}
		//	�������һ�����п����öԽ�
		if ( (char*)buffer + buflen == (char *)pFreePostMem )
		{
			Zoo_MemPool_List_Del( &pFreePostMem->point );
			__memmove( buffer , ( (char *)( pFreePostMem->start ) - sizeof( FREE_MEMORY_POOL ) ) , sizeof(FREE_MEMORY_POOL) );
			pFreePostMem = (FREE_MEMORY_POOL *)buffer;
			Zoo_MemPool_List_Add( &pFreePostMem->point , &pSysMem->head );
			pFreePostMem->length += buflen;
			break;
		}
		//	���ó��ֵĴ����Լ�����һ���ڴ��ĺ���
		//__asm int 3;
	}

	//	�������һ����
	while ( 0 != pFreePreMem && 0 == pFreePostMem )
	{
		//	�������һ�����п��м�û������
		if ( pFreePreMem->start + pFreePreMem->length < (char *)buffer )
		{
			pFreePreMem = (FREE_MEMORY_POOL *)buffer;
			pFreePreMem->length = buflen - sizeof( FREE_MEMORY_POOL );
			Zoo_MemPool_List_Add( &pFreePreMem->point , &pSysMem->head );
			break;
		}
		//	�������һ�����п����öԽ�
		if ( pFreePreMem->start + pFreePreMem->length == (char *)buffer )
		{
			pFreePreMem->length += buflen;
			break;
		}
		//	���ó��ֵĴ��� ���Լ�����һ��ģ���ǰ��
		//__asm int 3;
	}

	//	�������ڴ��м��һ��ռ�
	while ( 0 != pFreePreMem && 0 != pFreePostMem )
	{
		//	�������һ�����п����öԽ�
		if ( pFreePreMem->start + pFreePreMem->length == (char *)buffer )
		{
			pFreePreMem->length += buflen;
			//	�������һ�����п����öԽ�
			if ( ( (char*)buffer + buflen ) == (char *)pFreePostMem )
			{
				Zoo_MemPool_List_Del( &pFreePostMem->point );
				pFreePreMem->length += pFreePostMem->length + sizeof( FREE_MEMORY_POOL );

				//	ǰ�󶼿��ԶԽӣ��ѿ������ϵ��ڴ��ϲ���һ��Ȼ��
				break;
			}
		}
		//	�������һ�����п����öԽ�
		if ( (char*)buffer + buflen == ( (char *)( pFreePostMem->start ) - sizeof( FREE_MEMORY_POOL ) ) )
		{
			Zoo_MemPool_List_Del( &pFreePostMem->point );
			__memmove( buffer , ( (char *)( pFreePostMem->start ) - sizeof( FREE_MEMORY_POOL ) ) , sizeof(FREE_MEMORY_POOL) );
			pFreePostMem = (FREE_MEMORY_POOL *)buffer;
			Zoo_MemPool_List_Add( &pFreePostMem->point , &pSysMem->head );
			pFreePostMem->length += buflen;
			break;
		}

		//	û�жԽ�
		pFreePostMem = (FREE_MEMORY_POOL *)buffer;
		pFreePostMem->length = buflen - sizeof( FREE_MEMORY_POOL );
		Zoo_MemPool_List_Add( &pFreePostMem->point , &pSysMem->head );

		break;
	}

	return 0;
}