
// Linked list element
typedef struct list_node {
	struct list_node * next;
	struct tree_node * node;
} LIST_NODE;

// struct for tree elements 
typedef struct tree_node {
	int type;
	int inum;
	short nlink;
	uint * addrs;
	struct list_node * children;
	struct tree_node * parent;
	char * name;
} TREE_NODE;

