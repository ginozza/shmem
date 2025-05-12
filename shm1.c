#include <ctype.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#define MAX_LINES 1000
#define MAX_LINE_LEN 256

void error(const char* err) {
  perror(err);
  exit(1);
}

char *trim_leading(char *str) {
  while (isspace(*str)) str++;
  return str;
}

typedef struct {
  int start;
  int end;
  int lines_of_code;
  int keyword_count;
  int single_comments;
} Result;

Result analyze_lines_tokenized(char lines[][MAX_LINE_LEN], int start, int end, char *keyword) {
  Result res = {start, end, 0, 0, 0};
  const char *delim = " \t();{}[]=<>&|+-*/,\n";

  for (int i = start; i < end; i++) {
    char *line = lines[i];
    char copy[MAX_LINE_LEN];
    strcpy(copy, line);

    char *trimmed = trim_leading(copy);

    if (strncmp(trimmed, "//", 2) == 0) {
      res.single_comments++;
      continue;
    }

    if (strlen(trimmed) > 1)
      res.lines_of_code++;

    char *token = strtok(copy, delim);
    while (token != NULL) {
      if (strcmp(token, keyword) == 0)
        res.keyword_count++;
      token = strtok(NULL, delim);
    }
  }

  return res;
}

int count_file_lines(const char *filename) {
  FILE* file = fopen(filename, "r");
  if (!file) error("error opening file (count_file)");

  int count = 0;
  int ch;
  while ((ch = fgetc(file)) != EOF) {
    if (ch == '\n') {
      count++;
    }
  }

  fclose(file);
  return count;
}

int main(int argc, char *argv[]) {
  char* filename = argv[1];
  char* word = argv[2];
  int n_proc = atoi(argv[3]);
  if (n_proc <= 0 || n_proc > MAX_LINES) error("invalid number of processes");

  int n_lines = count_file_lines(filename);

  FILE* file = fopen(filename, "r");
  if (!file) error("error opening file");

  int shm_lines = shmget(IPC_PRIVATE, MAX_LINES * MAX_LINE_LEN, 0666 | IPC_CREAT);
  if (shm_lines == -1) error("shmget(lines)");
  char (*lines)[MAX_LINE_LEN] = shmat(shm_lines, NULL, 0);
  if (lines == (void*) -1) error("shmat(lines)");

  int shm_results = shmget(IPC_PRIVATE, n_proc * sizeof(Result), 0666 | IPC_CREAT);
  if (shm_results == -1) error("shmget(results)");
  Result* results = shmat(shm_results, NULL, 0);
  if (results == (void*) -1) error("shmat(results)");

  for (int i = 0; i < n_lines && fgets(lines[i], MAX_LINE_LEN, file); ++i);
  fclose(file);

  for (int i = 0; i < n_proc; ++i) {
    pid_t pid = fork();
    if (pid < 0) error("fork");

    if (pid == 0) {
      int start = i * (n_lines / n_proc);
      int end = (i == n_proc - 1) ? n_lines : (i + 1) * (n_lines / n_proc);
      results[i] = analyze_lines_tokenized(lines, start, end, word);
      exit(0);
    }
  }

  while (wait(NULL) > 0);

  Result total = {0};
  for (int i = 0; i < n_proc; ++i) {
    total.lines_of_code += results[i].lines_of_code;
    total.keyword_count += results[i].keyword_count;
    total.single_comments += results[i].single_comments;
  }

  printf("Total lineas de codigo     : %d\n", total.lines_of_code);
  printf("Total de busqueda de '%s' : %d\n", word, total.keyword_count);
  printf("Total comentarios de una linea: %d\n", total.single_comments);

  shmdt(lines);
  shmctl(shm_lines, IPC_RMID, NULL);
  shmdt(results);
  shmctl(shm_results, IPC_RMID, NULL);

  return 0;
}

