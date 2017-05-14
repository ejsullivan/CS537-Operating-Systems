#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <string.h>
#include <errno.h>

#define OK 1
#define ERROR -1
#define MAX_LENGTH 128
#define ABS(x) ((x < 0) ? -(x):x)

int key;
char tmpStr[128];
char word1[128];
char word2[128];

void clearBuffer(char * buffer)
{
  int i;
  for (i = 0; i < MAX_LENGTH; i++)
    buffer[i] = '\0'; 
}

void getWord(int index, char * string, char * retVal)
{
  char * val;
  char * tmpVal;
  char * delim = " ";
  int i; 

  strncpy(tmpStr, string, MAX_LENGTH);
  val = strtok(tmpStr, delim);

  for (i = 0; i < index; i++)
  {
    tmpVal = strtok(NULL, delim);
    if (tmpVal == NULL){
      break;
    }
    val = tmpVal;
  }

  strncpy(retVal, val, MAX_LENGTH);
  clearBuffer(tmpStr);
  return;
}

int checkLength(char * str)
{
  int i;
  for (i = 0; i < 128; i++){
    if (str[i] == '\n')
      return OK;
  }
  return ERROR; 
}

int indexStrCmp(const void * str1, const void * str2)
{
  int retVal;
  getWord(key, (char *) str1, word1);
  getWord(key, (char *) str2, word2); 
  retVal = strcmp(word1, word2);
  return retVal;
} 

int main(int argv, char **argc)
{
  struct stat * buf = malloc(sizeof(struct stat));
  char * filename, * ptr;
  char str[128];
  FILE * fp;
  int lines, i;
  off_t size;

  if (argv != 2 && argv != 3)
  {
    fprintf(stderr, "Error: Bad command line parameters\n");
    exit(1);
  }

  if ((argv == 3) && (argc[1][0] != '-'))
  {
    fprintf(stderr, "Error: Bad command line parameters\n");
    exit(1);
  }

  if (argv == 2)
  {
    key = 0;
    filename = argc[1];
  }
  else
  {
    key = ABS(strtol(argc[1], &ptr, 10)) - 1;
    if (key == ERROR)
    {
      fprintf(stderr, "Error: Bad command line parameters\n");
      exit(1);
    }
    filename = argc[2];
  }

  fp = fopen(filename, "r");

  if (fp == 0)
  {
    fprintf(stderr, "Error: Cannot open file %s\n", filename);
    exit(1);
  }

  if (ERROR == stat(filename, buf)){
    printf("ERROR: could not get file stats\n");
    return ERROR;
  }

  size = buf->st_size;
  lines = 0;
  while (NULL != fgets(str, 128, fp))
  {
    if (ERROR == checkLength(str))
    {
      fprintf(stderr, "Line too long\n");
      exit(1);
    }
    lines++;
  }
  rewind(fp);

  char page[lines][MAX_LENGTH];

  for (i = 0; i < lines; i++)
    fgets((char *) page[i], 128, fp);

  qsort(page, lines, MAX_LENGTH, indexStrCmp);

  for (i = 0; i < lines; i++){
    printf("%s", *(page + i));
  }
  
  return 0;
}
