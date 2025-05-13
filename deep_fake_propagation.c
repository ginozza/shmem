#include <semaphore.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#define N_NEIGHBORS 8
void error(const char *err) {
  perror(err);
  exit(EXIT_FAILURE);
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

int value_0_to_1(int **matrix, int x, int y, int rows, int cols) {
  int dx[] = {-1, -1, -1, 0, 0, 1, 1, 1};
  int dy[] = {-1, 0, 1, -1, 1, -1, 0, 1};

  int count = 0;
  for (int i = 0; i < N_NEIGHBORS; ++i) {
    int nx = x + dx[i];
    int ny = y + dy[i];
    if (nx >= 0 && ny >= 0 && nx < rows && ny < cols && matrix[nx][ny] > 0)
      count++;
  }

  if (matrix[x][y] == 0 && count >= 2)
    return 1;
  return matrix[x][y];
}

int value_1_to_2(int **matrix, int x, int y, int rows, int cols) {
  if (matrix[x][y] != 1)
    return matrix[x][y];

  int dx[] = {-1, -1, -1, 0, 0, 1, 1, 1};
  int dy[] = {-1, 0, 1, -1, 1, -1, 0, 1};
  int counter2 = 0;

  for (int i = 0; i < N_NEIGHBORS; ++i) {
    int nx = x + dx[i];
    int ny = y + dy[i];
    if (nx >= 0 && ny >= 0 && nx < rows && ny < cols) {
      if (matrix[nx][ny] == 2)
        counter2++;
    }
  }

  double P = 0.15 + (counter2 * 0.05);
  double r = (double)rand() / RAND_MAX;
  return (P > r) ? 2 : 1;
}

int main(int argc, char *argv[]) {
  if (argc < 3)
    error("invalid parameters: <shifts> <file>");

  int n_childs = 2;
  int shifts = atoi(argv[1]);
  const char *filename = argv[2];

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

  int shm_id_sem =
      shmget(IPC_PRIVATE, sizeof(sem_t) * n_childs * 2, 0666 | IPC_CREAT);
  sem_t *sems = shmat(shm_id_sem, NULL, 0);

  for (int i = 0; i < n_childs; ++i) {
    sem_init(&sems[i * 2], 1, 0);     // father_to_child[i]
    sem_init(&sems[i * 2 + 1], 1, 0); // child_to_father[i]
  }

  int chunk = rows / n_childs;

  for (int i = 0; i < 2; ++i) {
    pid_t pid = fork();
    if (pid == 0) {
      for (int t = 0; t < shifts; ++t) {
        sem_wait(&sems[i * 2]);

        if (i == 0) {
          for (int x = 0; x < rows; ++x)
            for (int y = 0; y < cols; ++y)
              mat_w[x][y] = value_0_to_1(mat_r, x, y, rows, cols);
        } else {
          for (int x = 0; x < rows; ++x)
            for (int y = 0; y < cols; ++y)
              mat_w[x][y] = value_1_to_2(mat_w, x, y, rows, cols);
        }

        sem_post(&sems[i * 2 + 1]);
      }

      shmdt(mat_r);
      shmdt(mat_w);
      shmdt(sems);
      exit(EXIT_SUCCESS);
    }
  }

  for (int t = 0; t < shifts; ++t) {
    sem_post(&sems[0 * 2]);
    sem_wait(&sems[0 * 2 + 1]);

    sem_post(&sems[1 * 2]);
    sem_wait(&sems[1 * 2 + 1]);

    printf("Paso %d:\n", t + 1);
    for (int x = 0; x < rows; ++x) {
      for (int y = 0; y < cols; ++y)
        printf("%3d", mat_w[x][y]);
      printf("\n");
    }
    printf("\n");

    for (int x = 0; x < rows; ++x)
      for (int y = 0; y < cols; ++y)
        mat_r[x][y] = mat_w[x][y];
  }

  while (wait(NULL) > 0)
    ;

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
