#include <stddef.h>
#include <stdlib.h>
#include <sys/ipc.h>
#include <unistd.h>
#include <stdio.h>
#include <sys/wait.h>
#include <sys/shm.h>

#define MAX_LINE 256

void error(const char *err) {
  perror(err);
  exit(1);
}

int *read_vector(FILE *file, size_t *vector_size) {
  char buffer[MAX_LINE];
  size_t count = 0;

  while (fgets(buffer, sizeof(buffer), file)) {
    count++;
  }

  int *vector = malloc(count * sizeof(int));
  if (!vector) error("malloc failed");

  rewind(file);
  *vector_size = 0;
  while (fgets(buffer, sizeof(buffer), file)) {
    vector[*vector_size] = atoi(buffer);
    (*vector_size)++;
  }

  return vector;
}


int find_max(int *vector, size_t v_size) {
  int max = vector[0];
  for (size_t i = 1; i < v_size; ++i)
    if (vector[i] > max)
      max = vector[i];
  return max;
}

int find_min(int *vector, size_t v_size) {
  int min = vector[0];
  for (size_t i = 1; i < v_size; ++i)
    if (vector[i] < min)
      min = vector[i];
  return min;
}

void sort_bucket(int *bucket, size_t size) {
  for (size_t i = 1; i < size; ++i) {
    int key = bucket[i];
    int j = i - 1;
    while (j >= 0 && bucket[j] > key) {
      bucket[j + 1] = bucket[j];
      j--;
    }
    bucket[j + 1] = key;
  }
}

int main(int argc, char *argv[]) {
  char *filename = argv[1];
  FILE *file = fopen(filename, "r");
  if (!file) error("error opening file");

  size_t v_size;
  int *vector = read_vector(file, &v_size);
  fclose(file);

  int max = find_max(vector, v_size);
  int min = find_min(vector, v_size);
  int bucket_size = atoi(argv[2]);
  int n_childs = (max - min) / bucket_size + 1;

  int shmid = shmget(IPC_PRIVATE, v_size * sizeof(int), 0666 | IPC_CREAT);
  if (shmid == -1) error("shmget");

  int *shared_vec = shmat(shmid, NULL, 0);
  if (shared_vec == (void *)-1) error("shmat");

  for (int i = 0; i < n_childs; ++i) {
    int pid = fork();
    if (pid < 0) error("fork");

    if (pid == 0) {
      int range_min = min + i * bucket_size;
      int range_max = range_min + bucket_size;

      int *bucket = malloc(v_size * sizeof(int));
      if (!bucket) error("malloc bucket");

      size_t count = 0;
      for (size_t j = 0; j < v_size; ++j) {
        if (vector[j] >= range_min && vector[j] < range_max) {
          bucket[count++] = vector[j];
        }
      }

      sort_bucket(bucket, count);

      size_t offset = 0;
      for (int k = 0; k < i; ++k) {
        for (size_t j = 0; j < v_size; ++j) {
          if (vector[j] >= (min + k * bucket_size) && vector[j] < (min + (k + 1) * bucket_size))
            offset++;
        }
      }

      for (size_t j = 0; j < count; ++j)
        shared_vec[offset + j] = bucket[j];

      free(bucket);
      shmdt(shared_vec);
      exit(0);
    }
  }

  while (wait(NULL) > 0);

  printf("Vector ordenado:\n");
  for (size_t i = 0; i < v_size; ++i)
    printf("%d\n", shared_vec[i]);

  shmdt(shared_vec);
  shmctl(shmid, IPC_RMID, NULL);

  free(vector);
  return 0;
}

