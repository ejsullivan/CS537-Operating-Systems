/* Checking if multiple downloader and parser threads execute concurrently */
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include "crawler.h"
#include <pthread.h>
#include <semaphore.h>
#include "helper.h"

pthread_mutex_t downloader_mutex, parser_mutex;
unsigned long downloaders[MAX_THREADS];
unsigned long parsers[MAX_THREADS];
int downloader_threads = 0;
int parser_threads = 0;
int wait_for_downloaders = 0;

char *fetch(char *link) {
  int fd = open(link, O_RDONLY);
  if (fd < 0) {
        fprintf(stderr, "failed to open file: %s", link);
	return NULL;
  }
  int size = lseek(fd, 0, SEEK_END);
  assert(size >= 0);
  char *buf = Malloc(size+1);
  buf[size] = '\0';
  assert(buf);
  lseek(fd, 0, SEEK_SET);
  char *pos = buf;
  while(pos < buf+size) {
    int rv = read(fd, pos, buf+size-pos);
    assert(rv > 0);
    pos += rv;
  }
  int i = 0;
  unsigned long current_id = (unsigned long) pthread_self();
  pthread_mutex_lock(&downloader_mutex);
  for(i = 0; i < MAX_THREADS; ++i)
  {
	if(downloaders[i] == -1)
	{
		downloader_threads++;
		downloaders[i] = current_id;
		break;
	}
	else if(downloaders[i] == current_id)
	{
		break;
	}
  }
  pthread_mutex_unlock(&downloader_mutex);
  if(wait_for_downloaders)
  {
	while(downloader_threads < 5)
		sleep(1);
  }
  if(strcmp(parseURL(link), "pagea") == 0)
  {
	wait_for_downloaders = 1;
  }
  close(fd);
  return buf;
}

void edge(char *from, char *to) {
  int i = 0;
  unsigned long current_id = (unsigned long) pthread_self();
  pthread_mutex_lock(&parser_mutex);
  for(i = 0; i < MAX_THREADS; ++i)
  {
	if(parsers[i] == -1)
	{
		parser_threads++;
		parsers[i] = current_id;
		break;
	}
	else if(parsers[i] == current_id)
	{
		break;
	}
  }
  pthread_mutex_unlock(&parser_mutex);
  if(strcmp(parseURL(from), "pagea") != 0)
  {
	while(parser_threads < 4)
		sleep(1);
  }
}

int main(int argc, char *argv[]) {
  pthread_mutex_init(&downloader_mutex, NULL);
  pthread_mutex_init(&parser_mutex, NULL);
  int i = 0;
  for(i = 0; i < MAX_THREADS; ++i)
  {
    downloaders[i] = -1;
    parsers[i] = -1;
  }  
  int rc = crawl("/u/c/s/cs537-1/ta/tests/4a/tests/files/num_threads/pagea", 5, 4, 15, fetch, edge);
  assert(rc == 0);
  printf("\ndownloader_threads : %d\n", downloader_threads);
  check(downloader_threads == 5, "Expected to spawn 5 download threads\n");
  printf("\nparser_threads : %d\n", parser_threads);
  check(parser_threads == 4, "Expected to spawn 4 parser threads\n");
  return 0;
}
