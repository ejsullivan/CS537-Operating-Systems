#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <stdint.h>
#include <pthread.h>
#include <regex.h>

#define HASH_SIZE 503

typedef struct __node { char *page; char * link; struct __node *next;} node;


pthread_mutex_t hash_lock = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t queue_size_lock = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t work_count_lock = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t bounded_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t unbounded_mutex = PTHREAD_MUTEX_INITIALIZER;

pthread_cond_t download_cond = PTHREAD_COND_INITIALIZER;
pthread_cond_t parse_cond = PTHREAD_COND_INITIALIZER;

node * parserRoot;
node * downloaderRoot;
node * hashTable[HASH_SIZE] = {NULL};
int maxQueueSize;
int queueSize;
int workCount;

void (*edge_fn)(char * from, char * to);
char * (*fetch_fn)(char * url);

void print_list() {
	node * curr = parserRoot;
	while(curr != NULL){
		printf("%s\n", curr->page);
		curr = curr->next;
	}
	return;
}

// code from https://en.wikipedia.org/wiki/Fletcher%27s_checksum
uint16_t fletcher16( uint8_t *data, int count ) {
	uint16_t sum1 = 0;
	uint16_t sum2 = 0;
	int index;
 
	for( index = 0; index < count; ++index )
	{
		sum1 = (sum1 + data[index]) % 255;
		sum2 = (sum2 + sum1) % 255;
	}
 
	return (sum2 << 8) | sum1;
}

void print_hash(char * link) {
	uint32_t hash;
	node * bucket;
	int length;
	length = strlen(link);
	hash = fletcher16((uint8_t *) link, length) % HASH_SIZE;
	//printf("hash = %d\n", hash);
	bucket = hashTable[hash];
	while(bucket != NULL){
		//printf("hashVal = %s\n", bucket->page);
		bucket = bucket->next;
	}
	return;
}

int hash_check(char * link) {
	uint32_t hash;
	node * bucket;
	int present = 0;
	hash = fletcher16((uint8_t *) link, strlen(link)) % HASH_SIZE;
	// Critical Section
	pthread_mutex_lock(&hash_lock);
	bucket = hashTable[hash];
	while(bucket != NULL){
		if(strcmp(bucket->page, link) == 0) {
			present = 1;
			break;
		}
		bucket = bucket->next;
	}
	pthread_mutex_unlock(&hash_lock);
	// End Critical Section
	return present;
}

// Always check to see if the value is in the hash table before you add
void hash_add(char * link) {
	uint32_t hash;
	node * bucket;
	hash = fletcher16((uint8_t *) link, strlen(link)) % HASH_SIZE;
	pthread_mutex_lock(&hash_lock);
	bucket = hashTable[hash];
	if(bucket == NULL){
		bucket = malloc(sizeof(node));
		bucket->page = link;
		bucket->next = NULL;
		hashTable[hash] = bucket;
	}
	else{
		while(bucket->next != NULL){
			bucket = bucket->next;
		}
		bucket->next = malloc(sizeof(node));
		bucket->next->page = link;
		bucket->next->next = NULL;
	}
	pthread_mutex_unlock(&hash_lock);
	return;
}

char * get_link() {
	char * str = NULL;
	node * temp;
	pthread_mutex_lock(&bounded_mutex);
	if(parserRoot != NULL) {
		pthread_cond_signal(&parse_cond);
		while(queueSize == 0) {
			//printf("get_link wait\n");
			pthread_cond_wait(&download_cond, &bounded_mutex);
		}
		queueSize--;
		temp = parserRoot;
		parserRoot = temp->next;
		str = temp->link;
		free(temp);
	}
	pthread_mutex_unlock(&bounded_mutex);
	return str;
}

void add_link(char * link) {	
	node * newNode = malloc(sizeof(node));
	newNode->link = link;
	newNode->page = NULL;
	newNode->next = NULL;

	pthread_mutex_lock(&work_count_lock);
	workCount++;
	pthread_mutex_unlock(&work_count_lock);

	pthread_mutex_lock(&bounded_mutex);
	pthread_cond_signal(&download_cond);
	while(queueSize == maxQueueSize) {
		//printf("add_link wait\n");
		pthread_cond_wait(&parse_cond, &bounded_mutex);
	}
	queueSize++;
	node * curr = parserRoot;
	if(parserRoot == NULL){
		parserRoot = newNode;
	}
	else {
		while(curr->next != NULL)
			curr = curr->next;
		curr->next = newNode;
	}
	pthread_mutex_unlock(&bounded_mutex);
	//pthread_mutex_lock(&work_count_lock);
	//workCount++;
	//pthread_mutex_unlock(&work_count_lock);
	return;
}

char * get_page(char ** link) {
	char * str;
	node * temp;	
	//pthread_mutex_lock(&page_lock);
	if(downloaderRoot != NULL){
		temp = downloaderRoot;
		downloaderRoot = temp->next;
		str = temp->page;
		*link = temp->link;
		free(temp);
	}
	else {
		*link = NULL;
		str = NULL;
	}
	//pthread_mutex_unlock(&page_lock);
	return str;
}

void add_page(char * page, char * link) {
	node * newNode = malloc(sizeof(node));
	newNode->page = page;
	newNode->link = link;
	newNode->next = NULL;
	node * curr = downloaderRoot;
	if(downloaderRoot == NULL){
		downloaderRoot = newNode;
	}
	else {
		while(curr->next != NULL) {
			curr = curr->next;
		}
		curr->next = newNode;
	}
	return;
}

int parse(char *page, char *link) {

	char *temp = NULL;
	char *tempStr = NULL;
	const char * delim = ":";
	char * savePtr;
	int count = 0;
	int firstRun = 1;
	//printf("parsing link = %s\n", link);

	temp = strtok_r(page, delim, &savePtr);
	while (temp != NULL) {
		if(strcmp(&temp[strlen(temp) - 5], " link") == 0 || strcmp(&temp[strlen(temp) - 5], "\nlink") == 0 
		   || (strcmp(&temp[strlen(temp) - 4], "link") == 0 && firstRun)){
			temp = strtok_r(NULL, delim, &savePtr);
			count = 0;
			while(temp[count] != '\n' && temp[count] != '\0' && temp[count] != ' ' && (temp[count] < 127))
				count++;
			tempStr = malloc(count + 1);
			strncpy(tempStr, temp, count);
			tempStr[count] = '\0';
			if(count != 0) {
				edge_fn((char *) link, (char *) tempStr);
				if(hash_check(tempStr) == 0) {
					//printf("ADD LINK\n");
					hash_add(tempStr);
					add_link(tempStr);
				}
			}
		}
		else
			temp = strtok_r(NULL, delim, &savePtr);
		if (firstRun == 1)
			firstRun = 0;
	}
	return 0;
}

void * downloader() {
	char * link;
	char * page;
	pthread_mutex_lock(&work_count_lock);
	while(workCount != 0) {
		//printf("START DOWNLOADER\n");
		pthread_mutex_unlock(&work_count_lock);
		link = get_link();
		if (link != NULL) {
			//printf("downloader tid = 0x%x\n", (unsigned int) pthread_self());
			//printf("link = %s\n", link);
			page = fetch_fn(link);
		}
		if (page != NULL && link != NULL) { 
			pthread_mutex_lock(&unbounded_mutex);
			add_page(page, link);
			pthread_mutex_unlock(&unbounded_mutex);
		}
		pthread_mutex_lock(&work_count_lock);
	}
	//printf("DOWNLOADER DONE\n");
	//printf("workCount = %d\n", workCount);
	pthread_mutex_unlock(&work_count_lock);
	pthread_mutex_lock(&queue_size_lock);
	queueSize = -1;
	pthread_mutex_unlock(&queue_size_lock);
	pthread_cond_signal(&download_cond);
	pthread_cond_signal(&parse_cond);
	return 0;
}

void * parser() {
	char * page;
	char * link;
	pthread_mutex_lock(&work_count_lock);
	while(workCount != 0) {
		//printf("START PARSE\n");
		//printf("workCount = %d\n", workCount);
		pthread_mutex_unlock(&work_count_lock);
		pthread_mutex_lock(&unbounded_mutex);
		//printf("GET PAGE\n");
		page = get_page(&link);
		//printf("GOT PAGE\n");
		pthread_mutex_unlock(&unbounded_mutex);
		//printf("page: %s\nlink: %s\n", page, link);
		if (page != NULL && link != NULL) {
			//printf("parser tid = 0x%x\n", (unsigned int) pthread_self());	
			parse(page, link);
			//printf("parse done\n");
			pthread_mutex_lock(&work_count_lock);
			workCount--;
			pthread_mutex_unlock(&work_count_lock);
			//printf("workCount--\n");
		}
		pthread_mutex_lock(&work_count_lock);
	}
	printf("PARSER DONE\n");
	//printf("workCount = %d\n", workCount);
	pthread_mutex_unlock(&work_count_lock);
	pthread_mutex_lock(&queue_size_lock);
	queueSize = -1;
	pthread_mutex_unlock(&queue_size_lock);
	pthread_cond_signal(&parse_cond);
	pthread_cond_signal(&download_cond);
	return 0;
}

int crawl(char *start_url,
	  int download_workers,
	  int parse_workers,
	  int queue_size,
	  char * (*_fetch_fn)(char *url),
	  void (*_edge_fn)(char *from, char *to)) {
 
	int i;
	pthread_t parsers[parse_workers];
	pthread_t downloaders[download_workers]; 
	edge_fn = _edge_fn;
	fetch_fn = _fetch_fn;
	maxQueueSize = queue_size;
	queueSize = 0;
	workCount = 0;
	parserRoot = NULL;
	downloaderRoot = NULL;

	hash_add(start_url);
	add_link(start_url);

	for(i = 0; i < download_workers; i++)
		pthread_create(&downloaders[i], NULL, downloader, (void *) NULL);

	for(i = 0; i < parse_workers; i++)
		pthread_create(&parsers[i], NULL, parser, (void *) NULL);	

	for(i = 0; i < download_workers; i++)
		pthread_join(downloaders[i], NULL);

	for(i = 0; i < parse_workers; i++)
		pthread_join(parsers[i], NULL);

	return 0;
}
