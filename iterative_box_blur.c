#include <stdlib.h>
#include <sys/ipc.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/shm.h>
#include <sys/wait.h>
#include <stdio.h>

#define N_NEIGHBORS 8

typedef struct {
  int rows;
  int cols;
  float mean;
  int **matrix;
} Window;

void error(const char *err) {
  perror(err);
  exit(EXIT_FAILURE);
}

Window read_file(FILE *file) {
  Window window;
  fscanf(file, "%d", &window.rows);
  fscanf(file, "%d", &window.cols);

  window.matrix = calloc(window.rows, sizeof(int *));
  for (int i = 0; i < window.rows; ++i)
    window.matrix[i] = calloc(window.cols, sizeof(int));

  for (int i = 0; i < window.rows; ++i)
    for (int j = 0; j < window.cols; ++j)
      fscanf(file, "%d", &window.matrix[i][j]);

  fclose(file);

  window.mean = .0;

  return window;
}

int mean_calc(int **matrix, int x, int y, int rows, int cols) {
  int dx[] = {-1, -1, -1, 0, 0, 1, 1, 1};
  int dy[] = {-1, 0, 1, -1, 1, -1, 0, 1};

  int sum = 0, n_real_neighbors = 0;

  for (int i = 0; i < N_NEIGHBORS; ++i) {
    int nx = x + dx[i];
    int ny = y + dy[i];

    if (nx >= 0 && ny >= 0 && nx < rows && ny < cols) {
      sum += matrix[nx][ny];
      n_real_neighbors++;
    }
  }

  return sum / n_real_neighbors;
}

int main(int argc, char *argv[]) {
  char *filename = argv[1];
  FILE *file = fopen(filename, "r");
  if (!file) error("error opening file"); 

  int n_childs = atoi(argv[2]);

  Window window_mat = read_file(file);
  size_t size_mat = window_mat.rows * window_mat.cols * sizeof(int);

  int shm_id_r = shmget(IPC_PRIVATE, size_mat, 0666 | IPC_CREAT);
  int *shm_r = (int *)shmat(shm_id_r, NULL, 0);
  int **mat_r = malloc(window_mat.rows * sizeof(int *));

  int shm_id_w = shmget(IPC_PRIVATE, size_mat, 0666 | IPC_CREAT);
  int *shm_w = (int *)shmat(shm_id_w, NULL, 0);
  int **mat_w = malloc(window_mat.rows * sizeof(int *));

  for (int i = 0; i < window_mat.rows; ++i) {
    mat_r[i] = &shm_r[i * window_mat.cols];
    mat_w[i] = &shm_w[i * window_mat.cols];
  }

  for (int i = 0; i < window_mat.rows; ++i)
    for (int j = 0; j < window_mat.cols; ++j)
      mat_r[i][j] = window_mat.matrix[i][j];

  for (int i = 0; i < window_mat.rows; ++i)
    free(window_mat.matrix[i]);
  free(window_mat.matrix);

  int miu = window_mat.rows / n_childs;
  
  for (int i = 0; i < n_childs; ++i) {
    pid_t pid = fork();

    if (pid == 0) {
      int start = i * miu;
      int end = (i == n_childs - 1) ? window_mat.rows : start + miu;

      for (int r = start; r < end; ++r){
        for (int c = 0; c < window_mat.cols; ++c) {
          mat_w[r][c] = mean_calc(mat_r, r, c, window_mat.rows, window_mat.cols);
        }
      }

      exit(EXIT_SUCCESS);
    }
  }

  while (wait(NULL) > 0);

  printf("Iterative box blur matrix:\n");
  for (int i = 0; i < window_mat.rows; ++i) {
    for (int j = 0; j < window_mat.cols; ++j) {
      printf("%5d", mat_w[i][j]);
    }
    printf("\n");
  }

  shmdt(shm_r);
  shmdt(shm_w);
  shmctl(shm_id_r, IPC_RMID, NULL);
  shmctl(shm_id_w, IPC_RMID, NULL);
  free(mat_r);
  free(mat_w);

  return EXIT_SUCCESS;
}
