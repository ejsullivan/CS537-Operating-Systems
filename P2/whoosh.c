#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdbool.h>
#include <fcntl.h>

#define MAX_BUFFER_SIZE 130

char * cmd;
char * cmdArgs;
char * outFile;
char * input;
char ** argList;
char ** pathVars;
int numPathVars;
int numArgs;
char * builtInCmds[] = {"exit", "cd", "pwd", "path"};
bool redirectStdOut = false;

void printError(void)
{
  char error_message[30] = "An error has occurred\n";
  write(STDERR_FILENO, error_message, strlen(error_message));
  fflush(stdout);
}

int checkLineLength(char * line)
{
  int i;
  for (i = 0; i < MAX_BUFFER_SIZE; i++)
  {
    if (line[i] == '\0')
      return 0;
  }
  return -1;
}

int callExit(void)
{
  exit(0);
}

void callCd(void)
{
  if (numArgs == 0)
  {
    chdir(getenv("HOME"));
  }
  else
    if(-1 == chdir(argList[1]))
      printError();
}

int callPwd(void)
{
  char buffer[MAX_BUFFER_SIZE];
  getcwd(buffer, MAX_BUFFER_SIZE);
  printf("%s\n", buffer);
  fflush(stdout);
  return 0;
}

int callPath(void)
{
  int i;
  for(i = 0; i < numPathVars; i++)
  {
    free(pathVars[i]);
  }
  free(pathVars);
  if (numArgs != 0)
  {
    pathVars = (char **) malloc(sizeof(char *) * numArgs);
    for (i = 0; i < numArgs; i++)
    {
      pathVars[i] = (char *) malloc(MAX_BUFFER_SIZE * sizeof(char));
      strncpy(pathVars[i], argList[i + 1], MAX_BUFFER_SIZE);
    }
    numPathVars = numArgs;
  }
  return 0;
}

int callEx(void)
{
  int i;
  char tempStr[MAX_BUFFER_SIZE];
  char tempFile[MAX_BUFFER_SIZE];
  FILE * fp;
  for (i = 0; i < numPathVars; i++)
  {
    strncpy(tempStr, pathVars[i], MAX_BUFFER_SIZE);
    strcat(tempStr, "/");
    strcat(tempStr, argList[0]);
    if(-1 != access(tempStr, F_OK))
      break;
  }
  if (0 == fork())
  {
    if (redirectStdOut)
    {
      strncpy(tempFile, outFile, MAX_BUFFER_SIZE);
      i = open(strcat(tempFile, ".out"), O_CREAT | O_TRUNC | O_WRONLY, S_IRUSR | S_IWUSR);
      if (-1 == i)
      {
        printError();
        exit(0);
        return -1;
      } 
      strncpy(tempFile, outFile, MAX_BUFFER_SIZE);  
      dup2(i, STDOUT_FILENO);
      strncpy(tempFile, outFile, MAX_BUFFER_SIZE);
      stderr = freopen(strcat(tempFile, ".err"), "w", stderr);
    }
    strncpy(argList[0], tempStr, MAX_BUFFER_SIZE);
    execv(tempStr, argList);
    printError();
    exit(0);
  }
  else
    wait();
  return 0;
}

int checkCommand(char * cmd)
{
  if (0 == strcmp(builtInCmds[0], cmd)){
    exit(0);
  }
  else if (0 == strcmp(builtInCmds[1], cmd))
    callCd();
  else if (0 == strcmp(builtInCmds[2], cmd))
    callPwd();
  else if (0 == strcmp(builtInCmds[3], cmd))
    callPath();
  else if (0 == strcmp("\n\0", cmd))
    return;
  else
    callEx();
}

int getInput(char *  buffer)
{ 
  int eofCheck;
  buffer[MAX_BUFFER_SIZE - 1] = 'x'; 
  fflush(stdin);
  if (NULL == fgets(buffer, MAX_BUFFER_SIZE, stdin))
    return -1;
 
  if (buffer[MAX_BUFFER_SIZE - 1] == '\0' && buffer[MAX_BUFFER_SIZE - 2] != '\n')
  {
    eofCheck = 0;
    while (eofCheck = fgetc(stdin) != '\n' && eofCheck != EOF){}
    return -1; 
  }
  return 0;
}

char * removeNewLine(char * string)
{
  if (0 == strcmp("\n\0", string))
    return string;
  char * tempStr = malloc(MAX_BUFFER_SIZE * sizeof(char));
  strncpy(tempStr, string, MAX_BUFFER_SIZE);
  return strtok(tempStr, "\n\0");
}

int processInput(char * input)
{
  char * strtokRet;
  char buffer[MAX_BUFFER_SIZE] = {0};
  char tempStr[MAX_BUFFER_SIZE] = {0};
  if (0 == strcmp(input, "\0"))
    return;
  strncpy(tempStr, input, MAX_BUFFER_SIZE);
  strncpy(cmd, strtok(tempStr, " "), MAX_BUFFER_SIZE);
  strtokRet = strtok(NULL, " ");
  numArgs = 0;
  while (NULL != strtokRet)
  {
    if (0 == strcmp(strtokRet, ">") || 0 == strcmp(strtokRet, ">\n"))
    {
      redirectStdOut = true;
      strtokRet = strtok(NULL, " ");
      if (strtokRet == NULL)
      {
        printError();
        return -1;
      }
      strncpy(outFile, strtokRet, MAX_BUFFER_SIZE);
      if (NULL != strtok(NULL, " "))
      {
        printError();
        return -1;
      }
      outFile = removeNewLine(outFile);
      break;
    }
    numArgs++;
    strcat(buffer, strtokRet);
    strcat(buffer, " ");
    strtokRet = strtok(NULL, " ");
  }
  strncpy(cmdArgs, buffer, MAX_BUFFER_SIZE); 
  return 0;
}

char ** getArgList(char ** list, char * stringBuffer)
{
  int i;
  char ** tempPtr;
  strncpy(stringBuffer, cmdArgs, MAX_BUFFER_SIZE);
  tempPtr = (char **) realloc(list, ((sizeof(char *) * numArgs) + 2 * sizeof(char *)));
  tempPtr[0] = removeNewLine(cmd);
  tempPtr[1] = strtok(stringBuffer, " ");
  for(i = 1; i < numArgs; i++)
  {
    tempPtr[i + 1] = strtok(NULL, " ");
  }
  tempPtr[numArgs + 1] = NULL;
  return tempPtr;
}

int main (int argc, char ** argv)
{
  int numChars;
  char * defaultPath = "/bin";
  if (argc > 1)
  {
    printError();
    exit(1);
  }
  cmd = (char *) malloc(MAX_BUFFER_SIZE * sizeof(char));
  cmdArgs = (char *) malloc(MAX_BUFFER_SIZE * sizeof(char));
  outFile = (char *) malloc(MAX_BUFFER_SIZE * sizeof(char));
  input = (char *) malloc(sizeof(char) * MAX_BUFFER_SIZE);
  argList = (char **) malloc(sizeof(char *));
  pathVars = (char **) malloc(sizeof(char *));
  pathVars[0] = (char *) malloc(MAX_BUFFER_SIZE * sizeof(char));
  numPathVars = 1;
  strncpy(pathVars[0], defaultPath, MAX_BUFFER_SIZE);
  
  int i;
  while(1)
  {
    fflush(stdout);
    printf("whoosh> ");
    fflush(stdout);
    if (-1 == getInput(input))
    {
      printError();
    }
    else if (0 == processInput(input))
    {
      argList = getArgList(argList, input);
      for (i = 0; i < numArgs + 1; i++)
        argList[i] = removeNewLine(argList[i]);
      checkCommand(removeNewLine(cmd));
    }
    fflush(stdout);
    redirectStdOut = false;
  } 
}
