#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <assert.h>
#include <sys/mman.h>
#include <stdbool.h>
#include <string.h>
#include "tree.h"

//
// Adds a new element to the specified linked list
//
void list_add(LIST_NODE ** head, TREE_NODE * newTreeNode) {
	LIST_NODE * newListNode;
	LIST_NODE * curr;
	newListNode = malloc(sizeof(LIST_NODE));
	newListNode->node = newTreeNode;
	newListNode->next = NULL;
	curr = *head;
	//printf("added tree_node with inum = %d\n", newTreeNode->inum);
	if (*head == NULL) {
		//printf("NULL HEAD\n");
		//printf("newListNode = %x\n", newListNode);
		*head = newListNode;
	}
	else {
		while (curr->next != NULL) {
			curr = curr->next;
		}
		curr->next = newListNode;
	}
	return;
}

//
// Creates a root node
//
TREE_NODE * create_tree(int type, int inum, short nlink, uint * addrs) {
	TREE_NODE * root;
	root = malloc(sizeof(TREE_NODE));
	root->type = type;
	root->inum = inum;
	root->nlink = nlink;
	root->addrs = addrs;
	root->children = NULL;
	// The root node is the only node with this property
	root->parent = root;
	root->name = "/";
	return root;
}

//
// Creates a new tree node
//
TREE_NODE * new_node(int type, int inum, short nlink, uint * addrs, TREE_NODE * parent, char * name) {
	TREE_NODE * newTreeNode;
	newTreeNode = malloc(sizeof(TREE_NODE));
	newTreeNode->type = type;
	newTreeNode->inum = inum;
	newTreeNode->nlink = nlink;
	newTreeNode->addrs = addrs;
	newTreeNode->children = NULL;
	newTreeNode->parent = parent;
	newTreeNode->name = name;
	return newTreeNode;
}

//
// Adds a new child to the parent nodes list of children
//
void add_tree_node(TREE_NODE * parent_node, TREE_NODE * child_node) {
	list_add(&(parent_node->children), child_node);	
}

//
// Creates a tree node and then adds it as a child to the specified parent
//
void add_new_node(TREE_NODE * parent_node, int type, int inum,
		  short nlink, uint * addrs, TREE_NODE * parent, char * name) {
	TREE_NODE * newNode;
	newNode = new_node(type, inum, nlink, addrs, parent, name);
	add_tree_node(parent_node, newNode);
}

//
// Gets the TREE_NODE specified by inum
//
TREE_NODE * get_node(TREE_NODE * root, int inum) {
	TREE_NODE * curr = NULL;
	LIST_NODE * child;
	if (root->inum == inum)
		return root;
	child = root->children;
	while (child != NULL) {
		curr = get_node(child->node, inum);
		if (curr != NULL)
			break;
		child = child->next;
	}
	return curr;
}

int get_ref_count(TREE_NODE * root, int inum) {
	int refCount = 0;
	TREE_NODE * curr = NULL;
	LIST_NODE * child;
	if (root->inum == inum)
		refCount++;
	child = root->children;
	while (child != NULL) {
		refCount += get_ref_count(child->node, inum);
		child = child->next;
	}
	return refCount;
}
