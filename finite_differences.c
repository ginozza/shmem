#include <semaphore.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/wait.h>
#include <unistd.h>

#define MAX_N 200

typedef struct {
  float D[2][MAX_N];
  sem_t sem_ready[MAX_N];
  sem_t sem_done[MAX_N];
  int shift;
} MemComp;

void error(const char* err) {
  perror(err);
  exit(EXIT_FAILURE);
}

int main(int argc, char* argv[]) {
  if (argc < 2) error("missing input file");

  char* file_name = argv[1];
  int n, t;
  FILE* file = fopen(file_name, "r");
  if (!file) error("error opening file");

  fscanf(file, "%d", &n);
  fscanf(file, "%d", &t);

  key_t key = ftok("shmfile", 65);
  int shmid = shmget(key, sizeof(MemComp), 0666 | IPC_CREAT);
  if (shmid == -1) error("error creating shmem");

  MemComp* mem = (MemComp*) shmat(shmid, NULL, 0);
  if (mem == (void *) -1) error("error requesting memory");

  for (int i = 0; i < n; ++i)
    fscanf(file, "%f", &mem->D[0][i]);

  for (int i = 0; i < n; ++i) {
    sem_init(&mem->sem_ready[i], 1, 0);
    sem_init(&mem->sem_done[i], 1, 0);
  }

  fclose(file);

  mem->shift = 0;

  for (int i = 0; i < n; ++i) {
    pid_t pid = fork();
    if (pid == 0) {
      for (int T = 0; T < t; ++T) {
        sem_wait(&mem->sem_ready[i]);

        int cur = mem->shift;
        int next = 1 - cur;

        float left = (i == 0) ? 0 : mem->D[cur][i - 1];
        float right = (i == n - 1) ? 0 : mem->D[cur][i + 1];
        float self = mem->D[cur][i];

        mem->D[next][i] = 0.25 * (left + 2 * self + right);

        sem_post(&mem->sem_done[i]);
      }
      exit(0);
    }
  }

  for (int T = 0; T < t; ++T) {
    int cur = mem->shift;
    int next = 1 - cur;

    for (int i = 0; i < n; ++i)
      sem_post(&mem->sem_ready[i]);

    for (int i = 0; i < n; ++i)
      sem_wait(&mem->sem_done[i]);

    printf("Shift %d: ", T + 1);
    for (int i = 0; i < n; ++i)
      printf("%.2f ", mem->D[next][i]);
    printf("\n");

    mem->shift = next;
  }

  while (wait(NULL) > 0);

  for (int i = 0; i < n; ++i) {
    sem_destroy(&mem->sem_ready[i]);
    sem_destroy(&mem->sem_done[i]);
  }

  shmdt(mem);
  shmctl(shmid, IPC_RMID, NULL);

  return EXIT_SUCCESS;
}

