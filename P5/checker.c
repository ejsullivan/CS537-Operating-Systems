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

// Block 0 is unused
// Block 1 is super block -> wsect(1, sb)
// Inodes start at block 2

#define ROOTINO 1 // root i-number
#define BSIZE 512 // block size

#define T_DIR  1   // Directory
#define T_FILE 2   // File
#define T_DEV  3   // Special device

#define BPB           (BSIZE*8)

// File system super block
struct superblock {
	uint size;					// Size of file system image (blocks)
	uint nblocks;				// Number of data blocks
	uint ninodes;				// Number of inodes
};

#define NDIRECT (12)

// On-disk inode structure
struct dinode {
	short type;					// File type
	short major;				// Major device number (T_DEV only)
	short minor;				// Minor device number (T_DEV only)
	short nlink;				// Number of links to inode in file system
	uint size;					// Size of file (bytes)
	uint addrs[NDIRECT+1];	// Data block addresses
};

// Directory is a file containing a sequence of dirent structures.
#define DIRSIZ 14

struct dirent {
  ushort inum;
  char name[DIRSIZ];
};

uint bitblocks;
int size = 1024;
void * img_ptr;
void * dbmap;
void * ibmap;
void * data;
struct superblock *sb;
struct dinode * root;
char * b_map;
TREE_NODE * tree;

// xv6 fs img
// similar to vsfs
// unused | superblock | inode table | bitmap (data) | data blocks
// (some gaps in here)


//
// Prints out the entire data bit map
//
void print_dbmap() {
	int i;
	for (i = 0; i < 995/8; i++)
		printf("0x%hhx\n", *((char *) dbmap + i));
}


//
// Returns if the block with the given address has been marked as valid in the data bit map
//
bool valid_block(int addr) {
	void * ptr;
	char ptrVal;
	ptr = dbmap + addr/8;
	//printf("ptr = %p\n", ptr);
	ptrVal = *((char *) ptr);
	if (ptrVal &  (1 << addr % 8) != 0)
		return true;
	return false;
}

void set_block(int addr) {
	char * ptr;
	char ptrVal;
	ptr = b_map + addr/8;
	ptrVal = *((char *) ptr);
	ptrVal = ptrVal | (1 << addr % 8);
	*ptr = ptrVal;
}

bool check_addrs(uint * addrs) {
	int i;
	for (i = 0; i < 12; i++) {
		if(!valid_block(addrs[i]))
			return false;
	}
	
}

uint * get_dblock(uint addr) {
	return (uint *) (addr*BSIZE + img_ptr);
}

//
// Returns a pointer to the ith inode
//
struct dinode * get_inode(int inum) {
	struct dinode *dip = (struct dinode *) (img_ptr + (2*BSIZE));
	dip += inum;
	return dip;
}

//
// Given a tree node this function recursivly adds all of the nodes connected to the node argument
//
void load_children(TREE_NODE * node) {
	struct dirent * currDirent;
	struct dinode * inode;
	bool dotdot, dot;
	LIST_NODE * child = NULL;
	// Get the first dirent struct from the data block
	currDirent = (struct dirent *) (node->addrs[0]*BSIZE + img_ptr);
	// Loop through the data block adding all of the inodes to the nodes children
	// also ensure that the . and .. dirs exist and are correct
	dot = false;
	dotdot= false;
	while (currDirent->inum != 0) {
		inode = get_inode(currDirent->inum);
		
		// Check that the inode is valid
		if (inode->type == 0) {
			fprintf(stderr, "ERROR: inode referred to in directory but marked free.\n");
			exit(1);
		}
		
		//printf("child name = %s\n", currDirent->name);
		if (strcmp(currDirent->name, ".") == 0)
			dot = true;
		else if (strcmp(currDirent->name, "..") == 0) {
			dotdot = true;
			if (currDirent->inum != node->parent->inum) {
				if (node->inum == 1)
					fprintf(stderr, "ERROR: root directory does not exist.\n");
				else
					fprintf(stderr, "ERROR: parent directory mismatch.\n");
				exit(1);
			}
		}
		else {
			// If the dir is referenced twice this is a problem
			if (inode->type == T_DIR && NULL != get_node(tree, currDirent->inum)) {
				fprintf(stderr, "ERROR: directory appears more than once in file system.\n");
				exit(1);
			}
			add_new_node(node, inode->type, currDirent->inum, inode->nlink, inode->addrs, node, currDirent->name);
		}
		currDirent++;
	}

	// Check that both the dot and dotdot dirs exist
	if (!dot  || !dotdot) {
		fprintf(stderr, "ERROR: directory not properly formatted.\n");
		exit(1);
	}	

	// Recursivly load the grandchildren
	child = node->children;
	while (child != NULL) {
		if (child->node->type == T_DIR)
			load_children(child->node);
		child = child->next;	
	}

	return;
}

//
// Loads the file system into a tree structure, assumes the address of the bitmap and datablocks has been
// calculated ahead of time
//
TREE_NODE * load_fs() {
	TREE_NODE * root;
	struct dinode *dip = (struct dinode *) (img_ptr + (2*BSIZE));
	// The first node is junk data	
	dip++;

	if (dip->type != T_DIR) {
		fprintf(stderr, "ERROR: root directory does not exist.\n");
		exit(1);
	}	

	// The first node to load in is the root node.
	root = create_tree(dip->type, 1, dip->nlink, dip->addrs);
	tree = root;

	// After the root node add the files/dirs it points to.
	load_children(root);
	return root;			
}


//
// Looks through the inodes and confirms that if a given inode is valid 
// it is referenced at least once, its parent is correct, 
//
void inode_test() {
	TREE_NODE * curr;
	LIST_NODE * child;
	struct dinode * inode;
	struct dinode * tmp;
	int numNodes, numBlocks, i, j, k, l;
	uint * dblock;
	numNodes = sb->ninodes;
	numBlocks = sb->nblocks;

	// Check root dir
	//curr = get_node(root, 1);

	for (i = 1; i < numNodes; i++) {
		//printf("i = %d\n", i);
		inode = get_inode(i);
		// Make sure all of the inodes are either unallocated or a valid type 
		if (inode->type != 0 && inode->type != T_FILE && inode->type != T_DIR && inode->type != T_DEV) {
			fprintf(stderr, "ERROR: bad inode.\n"); 
			exit(1);
		}
	
		// Check that all the vals in addrs are valid
		if (inode->type != 0) {

			// Check for dups
			for (j = 0; j < 13; j++) {
				for (l = 0; l < numNodes; l++) {
					tmp = get_inode(l);
					for (k = j + 1; k < 13; k++) {
						if (inode->addrs[j] == tmp->addrs[k] && tmp->addrs[k] != 0 && inode->addrs[j] != 0) {
							//printf("i = %d j = %d k = %d\n", i, j, k);
							fprintf(stderr, "ERROR: address used more than once.\n");
							exit(1);
						}
					}
				}
			}
			
			for (j = 0; j < 13; j++) {
				//printf("Check addr = 0x%x\n", inode->addrs[j]);
				if (!valid_block(inode->addrs[j])) {
					fprintf(stderr, "ERROR: bad address in inode.\n");
					exit(1);
				}
				if (inode->addrs[j] > 1000) {
					fprintf(stderr, "ERROR: bad address in inode.\n");
					exit(1);
				}
			}
			
			// Check all of the addrs in the indirect ptr
			if (inode->addrs[12] != 0) {
				j = 0;
				dblock = get_dblock(inode->addrs[12]);
				while (*dblock != 0 && j < BSIZE/sizeof(uint)) {
					if (!valid_block(*dblock)) {
						fprintf(stderr, "ERROR: bad address in inode.\n");
						exit(1);
					}

					if (*dblock > 1000) {
						fprintf(stderr, "ERROR: bad address in inode.\n");
						exit(1);
					}
					j++;
					dblock++;	
				}
			}
		}
	}

	// inodes are correct load the fs now
	//printf("LOAD FS\n");
	tree = load_fs();

	// Check that if an inode is in use it is in the tree 
	// and the number of reference counts in the inode matches the number of references in the tree
	for (i = 1; i < numNodes; i++) {
		inode = get_inode(i);
		if (inode->type != 0 && NULL == get_node(tree, i)) {
			fprintf(stderr, "ERROR: inode marked use but not found in a directory.\n");
			exit(1);
		}
		if (inode->type == T_FILE && get_ref_count(tree, i) != inode->nlink) {
			fprintf(stderr, "ERROR: bad reference count for file.\n");
			exit(1);
		}
	}
	inode = get_inode(1);
}	

int main(int argc, char *argv[]) {

	int fd = open(/*"fs.img"*/argv[1], O_RDONLY);

	if (fd <= 0) {
		fprintf(stderr, "image not found.\n");
		exit(1);
	}

	//assert(fd > 0);

	int rc;
	struct stat sbuf;
	TREE_NODE * tree;	

	rc = fstat(fd, &sbuf);	
	assert (rc == 0);

	// use mmap()
	img_ptr = mmap(NULL, sbuf.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
	assert(img_ptr != MAP_FAILED);

	sb = (struct superblock *) (img_ptr + BSIZE);	
	bitblocks = size/(512*8) + 1;
	dbmap = (void *)sb + sb->ninodes*sizeof(struct dinode) + sb->ninodes*sizeof(struct dinode) % BSIZE + 2*BSIZE;
	data = (dbmap + (bitblocks * BSIZE));
	b_map = malloc(data - dbmap);
	
	//print_dbmap();
	inode_test();
	
	return 0;
}
