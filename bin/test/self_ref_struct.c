typedef struct node_t node_t;
typedef struct node_t
{
	node_t* m_Prev;
	node_t* m_Next;
	void* m_Data;
} node_t;

void linkNext(node_t* node, node_t* next)
{
	node->m_Next = next;
	next->m_Prev = node;
}