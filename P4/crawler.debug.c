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


pthread_mutex_t page_lock = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t link_lock = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t hash_lock = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t size_lock = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t thread_count_lock = PTHREAD_MUTEX_INITIALIZER;

pthread_cond_t download_cond = PTHREAD_COND_INITIALIZER;
pthread_cond_t parse_cond = PTHREAD_COND_INITIALIZER;
 
node * parserRoot;
node * downloaderRoot;
node * hashTable[HASH_SIZE] = {NULL};
int maxQueueSize;
int queueSize;
int thread_count;
int end;

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
	// Critical Section
	pthread_mutex_lock(&hash_lock);
	//printf("Start critical section\n");
	bucket = hashTable[hash];
	if(bucket == NULL){
		bucket = malloc(sizeof(node));
		bucket->page = link;
		bucket->next = NULL;
		hashTable[hash] = bucket;
	}
	else{
		while(bucket->next != NULL){
			//printf("AHHHH\n");
			bucket = bucket->next;
		}
		bucket->next = malloc(sizeof(node));
		bucket->next->page = link;
		bucket->next->next = NULL;
	}
	//printf("End critical section\n");
	pthread_mutex_unlock(&hash_lock);
	//printf("return\n");
	// End Critical Section
	return;
}

char * get_link() {
	char * str = NULL;
	node * temp;
	// Critical Section	
	pthread_mutex_lock(&link_lock);
	if(parserRoot != NULL){
		pthread_mutex_lock(&size_lock);
		queueSize--;
		pthread_mutex_unlock(&size_lock);
		temp = parserRoot;
		parserRoot = temp->next;
		str = temp->link;
		free(temp);
	}
	pthread_mutex_unlock(&link_lock);
	// End Critical Section
	return str;
}

void add_link(char * link) {
	node * curr = parserRoot;
	node * newNode = malloc(sizeof(node));
	newNode->link = link;
	newNode->page = NULL;
	newNode->next = NULL;
	pthread_mutex_lock(&size_lock);
	queueSize++;
	pthread_mutex_unlock(&size_lock);
	// Critical Section
	pthread_mutex_lock(&link_lock);
	if(parserRoot == NULL){
		parserRoot = newNode;
		goto add_link_unlock;
	}
	while(curr->next != NULL)
		curr = curr->next;
	curr->next = newNode;
add_link_unlock:
	pthread_mutex_unlock(&link_lock);
	// End Critical Section
	return;
}

char * get_page(char ** link) {
	char * str;
	node * temp;	
	pthread_mutex_lock(&page_lock);
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
	pthread_mutex_unlock(&page_lock);
	return str;
}

void add_page(char * page, char * link) {
	node * curr = downloaderRoot;
	node * newNode = malloc(sizeof(node));
	newNode->page = page;
	newNode->link = link;
	newNode->next = NULL;
	// Critical Section
	pthread_mutex_lock(&page_lock);
	if(downloaderRoot == NULL){
		downloaderRoot = newNode;
		goto add_page_unlock;
	}
	while(curr->next != NULL) {
		printf("curr->next = %p\n", curr->next);
		curr = curr->next;
		printf("##### curr = %p #####\n", curr);
	}
	curr->next = newNode;
	add_page_unlock:
	pthread_mutex_unlock(&page_lock);
	// End Critical Section
	return;
}

int parse(char *page, char *link) {

	char *temp = NULL;
	char *tempStr = NULL;
	const char *delim = ":";
	char * savePtr;
	int count = 0;

	temp = strtok_r(page, delim, &savePtr);
	while (temp != NULL) {
		if(strcmp(&temp[strlen(temp) - 4], "link") == 0){
			temp = strtok_r(NULL, delim, &savePtr);
			count = 0;
			while(temp[count] != '\n' && temp[count] != '\0' && temp[count] != ' ' && (temp[count] < 127))
				count++;
			//printf("count = %d\n", count);
			tempStr = malloc(count + 1);
			strncpy(tempStr, temp, count);
			tempStr[count] = '\0';
			//printf("temp = %s\n", temp);
			//printf("tempStr = %s\n", tempStr);
			//printf("length of tempStr = %d\n", (int) strlen(tempStr));
			//printf("last char = %c = %d\n", tempStr[strlen(tempStr) - 1], tempStr[strlen(tempStr) - 1]); 
			edge_fn((char *) link, (char *) tempStr);
			if(hash_check(tempStr) == 0) {
				printf("ADD LINK\n");
				hash_add(tempStr);
				print_hash(tempStr);
				add_link(tempStr);
			}
		}
		else
			temp = strtok_r(NULL, delim, &savePtr);
	}

	return 0;
}

void * downloader() {
	printf("DOWNLOAD!\n");
	char * link;
	char * page;
	while(1) {
		//printf("START DOWNLOAD\n");
		link = get_link();
		//printf("link = %s\n", link);
		if(link != NULL) {
			page = fetch_fn(link);
			add_page(page, link);
			printf("Signal parser\n");
			pthread_cond_signal(&parse_cond);
			//free(link);	 	
		}
		//printf("PARSE 1\n");
		//printf("DOWNLOADER CHECK IF DONE\n");
		pthread_mutex_lock(&link_lock);
		//printf("GET LOCK\n");
		//pthread_mutex_lock(&page_lock);
		/*if(parserRoot == NULL && downloaderRoot == NULL) {
			printf("DOWNLOADER DONE\n");
			pthread_mutex_unlock(&link_lock);
			pthread_mutex_unlock(&page_lock);
			break;
		}*/
		//pthread_mutex_unlock(&page_lock);
		//printf("downloaderRoot link = %s\n", downloaderRoot->link);
		while(parserRoot == NULL) {
			pthread_mutex_lock(&thread_count_lock);
			thread_count--;
			pthread_mutex_unlock(&thread_count_lock);
			if(thread_count == 0) {
				if(parserRoot == NULL && downloaderRoot == NULL && thread_count == 0 && end == 0) {
					printf("DOWNLOADER DONE\n");
					pthread_mutex_unlock(&link_lock);
					pthread_mutex_unlock(&page_lock);
					printf("DOWNLOADER BROADCASTING\n");
					pthread_cond_broadcast(&parse_cond);
					pthread_cond_broadcast(&download_cond);
					end = 1;
					return 0;
				}
			}
			if(end == 1) {
				printf("DEAD DOWNLOADER\n");
				pthread_mutex_unlock(&link_lock);
				pthread_cond_broadcast(&parse_cond);
				pthread_cond_broadcast(&download_cond);
				return 0;
			}
			else {
				printf("Download wait!!!\n");
				pthread_cond_wait(&download_cond, &link_lock);
				printf("DOWNLOADER WAKE UP\n");
			}

			pthread_mutex_lock(&thread_count_lock);
			thread_count++;
			printf("thread_count++ = %d\n", thread_count);
			pthread_mutex_unlock(&thread_count_lock);
			printf("end = %d\n", end);
			
		}
		//printf("PARSE 2\n");
		pthread_mutex_unlock(&link_lock);
	}
	return 0;
}

void * parser() {
	printf("PARSE!\n");
	char * page;
	char * link;
	while(1) {
		//printf("START PARSE\n");
		page = get_page(&link);
		printf("page = %s link = %s\n", page, link);
		if(page != NULL && link != NULL){
			parse(page, link);
			//free(page);
			printf("Signal downloader\n");
			pthread_cond_signal(&download_cond);
		}	

		//printf("PARSER CHECK IF DONE\n");
		//pthread_mutex_lock(&link_lock);
		pthread_mutex_lock(&page_lock);
		/*if(parserRoot == NULL && downloaderRoot == NULL) {
			printf("PARSER DONE\n");
			pthread_mutex_unlock(&link_lock);
			pthread_mutex_unlock(&page_lock);
			break;
		}*/
		//pthread_mutex_unlock(&link_lock);
		while(downloaderRoot == NULL) {
			pthread_mutex_lock(&thread_count_lock);
			thread_count--;
			pthread_mutex_unlock(&thread_count_lock);
			if(thread_count == 0) {				
				if(parserRoot == NULL && downloaderRoot == NULL && thread_count == 0 && end == 0) {
					printf("PARSER DONE\n");
					pthread_mutex_unlock(&link_lock);
					pthread_mutex_unlock(&page_lock);
					printf("PARSER BROADCASTING\n");
					pthread_cond_broadcast(&parse_cond);
					pthread_cond_broadcast(&download_cond);
					end = 1;
					return 0;
				}
			}
			if(end == 1) {
				printf("DEAD PARSER\n");
				pthread_mutex_unlock(&size_lock);
				pthread_mutex_unlock(&page_lock);
				pthread_cond_broadcast(&parse_cond);
				pthread_cond_broadcast(&download_cond);
				return 0;
			}
			else {
				printf("Parser wait\n");
				pthread_cond_wait(&parse_cond, &page_lock);
				printf("PARSER WAKE UP\n");
			}
			pthread_mutex_lock(&thread_count_lock);
			thread_count++;
			printf("thread_count = %d\n", thread_count);
			pthread_mutex_unlock(&thread_count_lock);
		}
		pthread_mutex_unlock(&page_lock);

		pthread_mutex_lock(&size_lock);
		printf("Go to the loop\n");
		printf("queueSize = %d\n", queueSize);
		while(queueSize >= maxQueueSize) {
			//pthread_cond_signal(&download_cond);
			pthread_mutex_lock(&thread_count_lock);
			thread_count--;
			if(thread_count == 0) {
				if(parserRoot == NULL && downloaderRoot == NULL && thread_count == 0 && end == 0) {
					printf("PARSER DONE\n");
					pthread_mutex_unlock(&link_lock);
					pthread_mutex_unlock(&page_lock);
					pthread_mutex_unlock(&thread_count_lock);
					printf("PARSER BROADCASTING\n");
					pthread_cond_broadcast(&parse_cond);
					pthread_cond_broadcast(&download_cond);
					end = 1;
					return 0;
				}
			}
			if(end == 1) {
				pthread_mutex_unlock(&thread_count_lock);
				pthread_mutex_unlock(&size_lock);
				pthread_cond_broadcast(&parse_cond);
				pthread_cond_broadcast(&download_cond);
				return 0;
			}
			else {
				printf("Parser wait\n");
				pthread_mutex_unlock(&thread_count_lock);
				pthread_cond_wait(&parse_cond, &size_lock);
				printf("PARSER WAKE UP\n");
			}
			pthread_mutex_lock(&thread_count_lock);
			thread_count++;
			pthread_mutex_unlock(&thread_count_lock);
		}
		pthread_mutex_unlock(&size_lock);
		printf("DO THE LOOP AGAIN\n");
	}
	return 0;
}

int crawl(char *start_url,
	  int download_workers,
	  int parse_workers,
	  int queue_size,
	  char * (*_fetch_fn)(char *url),
	  void (*_edge_fn)(char *from, char *to)) {
 
	int i;
	printf("START!!!\n");
	pthread_t parsers[parse_workers];
	pthread_t downloaders[download_workers]; 
	char *page;
	edge_fn = _edge_fn;
	fetch_fn = _fetch_fn;
	//char *temp;
	maxQueueSize = queue_size;
	queueSize = 0;
	end = 0;
	thread_count = download_workers + parse_workers;	
	printf("thread_count: %d\n", thread_count);
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

	printf("ALL THREADS DONE!\n");

	//print_hash(start_url);
	//if (hash_check(start_url) == 0)
		//printf("WHY DO YOU DO THIS\n");
	//page = _fetch_fn(start_url);
	//parse(page, _edge_fn, start_url);
	//page = _fetch_fn(start_url);
	//parse(page, _edge_fn, start_url);
	//print_list();
	page = get_link();
	while(page != NULL){
		printf("%s\n", page);
		page = get_link();
	}
	printf("DONE\n");
	return 0;
}
