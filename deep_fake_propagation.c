#include <stdio.h>
#include <stdlib.h>
#include <sys/ipc.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/shm.h>
#include <sys/wait.h>
#include <semaphore.h>

#define N_NEIGHBORS 8
void error(const char *err) {
  perror(err);
  exit(EXIT_FAILURE);
}

int value(int **matrix, int x, int y, int rows, int cols) {
  int dx[] = {-1, -1, -1, 0, 0, 1, 1, 1};
  int dy[] = {-1, 0, 1, -1, 1, -1, 0, 1};

  int counter1 = 0;
  int counter2 = 0;

  for (int i = 0; i < N_NEIGHBORS; ++i) {
    int nx = x + dx[i];
    int ny = y + dy[i];

    if (nx >= 0 && ny >= 0 && nx < rows && ny < cols) {
      if (matrix[nx][ny] == 1)
        counter1++;
      else if (matrix[nx][ny] == 2)
        counter2++;
    }
  }

  int current_value = matrix[x][y];
  int new_value = 0;
  if (counter1 + counter2 >= 2 && current_value == 0)
    new_value = 1;
  if (current_value == 2)
    new_value = 2;
  if (current_value == 1){
    double P = 0.15 + (counter2 * 0.05);
    double r = (double)rand() / RAND_MAX;
    new_value = (P > r) ? 2 : 1;
  }

  return new_value;
}

int **read_file(const char *filename, int *rows, int *cols) {
  FILE *file = fopen(filename, "r");
  if (!file)
    error("error opening file");

  fscanf(file, "%d", rows);
  fscanf(file, "%d", cols);

  int **matrix = calloc(*rows, sizeof(int *));
  for (int i = 0; i < *rows; ++i) {
    matrix[i] = calloc(*cols, sizeof(int));
  }

  for (int i = 0; i < *rows; ++i)
    for (int j = 0; j < *cols; ++j)
      fscanf(file, "%d", &matrix[i][j]);

  fclose(file);
  return matrix;
}

int main(int argc, char *argv[]) {
  if (argc < 4)
    error("invalid parameters: <n_childs> <shifts> <file>");

  int n_childs = atoi(argv[1]);
  int shifts = atoi(argv[2]);
  const char *filename = argv[3];

  int rows, cols;
  int **temp_matrix = read_file(filename, &rows, &cols);

  size_t size_mat = rows * cols * sizeof(int);
  int shm_id_r = shmget(IPC_PRIVATE, size_mat, 0666 | IPC_CREAT);
  int *shm_r = (int *)shmat(shm_id_r, NULL, 0);
  int **mat_r = malloc(rows * sizeof(int *));

  int shm_id_w = shmget(IPC_PRIVATE, size_mat, 0666 | IPC_CREAT);
  int *shm_w = (int *)shmat(shm_id_w, NULL, 0);
  int **mat_w = malloc(rows * sizeof(int *));

  for (int i = 0; i < rows; ++i) {
    mat_r[i] = &shm_r[i * cols];
    mat_w[i] = &shm_w[i * cols];
  }

  for (int i = 0; i < rows; ++i)
    for (int j = 0; j < cols; ++j)
      mat_r[i][j] = temp_matrix[i][j];

  for (int i = 0; i < rows; ++i) 
    free(temp_matrix[i]);
  free(temp_matrix);

  int shm_id_sem = shmget(IPC_PRIVATE, sizeof(sem_t) * n_childs * 2, 0666 | IPC_CREAT);
  sem_t *sems = shmat(shm_id_sem, NULL, 0);

  for (int i = 0; i < n_childs; ++i) {
    sem_init(&sems[i * 2], 1, 0); // father_to_child[i]
    sem_init(&sems[i * 2 + 1], 1, 0); // child_to_father[i]
  }

  int chunk = rows / n_childs;

  for (int i = 0; i < n_childs; ++i) {
    pid_t pid = fork();
    if(pid == 0) {
      int child_idx = i;
      int start = i * chunk;
      int end = (i == n_childs - 1) ? rows : start + chunk;

      for (int t = 0; t < shifts; ++t) {
        sem_wait(&sems[child_idx * 2]);
        for (int i = start; i < end; ++i) {
          for (int j = 0; j < cols; ++j) {
            mat_w[i][j] = value(mat_r, i, j, rows, cols);
          }
        }
        sem_post(&sems[child_idx * 2 + 1]);
      }
      shmdt(mat_r);
      shmdt(mat_w);
      shmdt(sems);
      exit(EXIT_SUCCESS);
    }
  }

  
  for (int t = 0; t < shifts; ++t) {
    // unblock childs
    for (int i = 0; i < n_childs; ++i)
      sem_post(&sems[i * 2]);

    // block childs for next shift 
    for (int i = 0; i < n_childs; ++i)
      sem_wait(&sems[i * 2 + 1]);

    printf("Paso %d:\n", t + 1);
    for (int i = 0; i < rows; ++i) {
      for (int j = 0; j < cols; ++j)
        printf("%3d", mat_r[i][j]);
      printf("\n");
    }
    printf("\n");

    for (int i = 0; i < rows - 0; ++i)
      for (int j = 0; j < cols - 0; ++j)
        mat_r[i][j] = mat_w[i][j];
  }

  while (wait(NULL) > 0);

  for (int i = 0; i < n_childs + 2; ++i)
    sem_destroy(&sems[i]);

  shmdt(shm_r);
  shmdt(shm_w);

  free(mat_r);
  free(mat_w);

  shmctl(shm_id_r, IPC_RMID, NULL);
  shmctl(shm_id_w, IPC_RMID, NULL);

  shmdt(sems);
  shmctl(shm_id_sem, IPC_RMID, NULL);

  return 0; 
}
