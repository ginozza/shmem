#include <stdio.h>
#include <stdlib.h>
#include <sys/ipc.h>
#include <unistd.h>
#include <sys/shm.h>
#include <sys/wait.h>

#define N_MAX 1000

void error(const char* err) {
  perror(err);
  exit(1);
}

typedef struct {
  int start;
  int end;
  int n_count;
  int n_pos[N_MAX];
} SearchChunk;

int main(int argc, char* argv[]) {
  // Format e.j: ./ps 4 input.txt 5
  if (argc < 3) error("invalid format");
  int n_proc = atoi(argv[1]);

  char* filename = argv[2];
  FILE* file = fopen(filename, "r");
  if (!file) error("error opening file (main)");

  int target = atoi(argv[3]);

  int n_num;
  char buffer[1024];
  if (!fgets(buffer, sizeof(buffer), file))
    error("error reading number size");

  n_num = atoi(buffer);

  int *num_arr = (int *)malloc(n_num * sizeof(int));
  if (!num_arr)
    error("malloc error (num_arr)");

  for (int i = 0; i < n_num; ++i) {
    if (!fgets(buffer, sizeof(buffer), file))
      error("error reading data line");
    num_arr[i] = atoi(buffer);
  }

  fclose(file);

  int shmid = shmget(IPC_PRIVATE, n_proc * sizeof(SearchChunk), 0666 | IPC_CREAT);
  if (shmid == -1) error("shmget");

  SearchChunk* chunks = shmat(shmid, NULL, 0);
  if (chunks == (void*) -1) error("shmat");

  for (int i = 0; i < n_proc; ++i) {
    int pid = fork();
    if (pid < 0) error("fork");

    if (pid == 0) {
      int start = i * (n_num / n_proc);
      int end = (i == n_proc - 1) ? n_num : (i + 1) * (n_num / n_proc);

      chunks[i].start = start;
      chunks[i].end = end;
      chunks[i].n_count = 0;

      int count = 0;
      for (int j = start; j < end; ++j) {
        if (num_arr[j] == target)
          chunks[i].n_pos[count++] = j;
      } 
      chunks[i].n_count = count;
      shmdt(chunks);
      free(num_arr);
      exit(0);
    }
  }

  while (wait(NULL) > 0);

  int total_finds = 0;
  for (int i = 0; i < n_proc; ++i) {
    printf("Proceso %d: rango [%d - %d], encontró %d veces en posiciones:\n", i, chunks[i].start, chunks[i].end, chunks[i].n_count);

    for (int j = 0; j < chunks[i].n_count; ++j) {
      printf("  pos %d\n", chunks[i].n_pos[j]);
    }
    total_finds += chunks[i].n_count;
  }

  printf("Total encontrado: %d\n", total_finds);

  shmdt(chunks);
  shmctl(shmid, IPC_RMID, NULL);
  free(num_arr);
  return 0;
}
