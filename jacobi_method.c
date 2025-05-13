#include <fcntl.h>
#include <semaphore.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/shm.h>
#include <sys/wait.h>
#include <unistd.h>

int value(int **mat, int x, int y) {
  int top = mat[x - 1][y];
  int left = mat[x][y - 1];
  int right = mat[x][y + 1];
  int bottom = mat[x + 1][y];
  return (top + left + right + bottom) / 4;
}

size_t sizeof_dm(int rows, int cols, size_t element_size) {
  return rows * sizeof(void *) + (rows * cols * element_size);
}

void create_index(void **matrix, int rows, int cols, size_t element_size) {
  size_t row_size = cols * element_size;
  matrix[0] = matrix + rows;
  for (int i = 1; i < rows; i++) {
    matrix[i] = (char *)matrix[i - 1] + row_size;
  }
}

int **read_file(const char *filename, int *rows, int *cols) {
  FILE *file = fopen(filename, "r");
  if (!file) {
    perror("Cannot open file");
    exit(EXIT_FAILURE);
  }
  fscanf(file, "%d", rows);
  fscanf(file, "%d", cols);

  int **matrix = calloc(*rows, sizeof(int *));
  for (int i = 0; i < *rows; i++) {
    matrix[i] = calloc(*cols, sizeof(int));
  }

  for (int i = 0; i < *rows; i++)
    for (int j = 0; j < *cols; j++)
      fscanf(file, "%d", &matrix[i][j]);

  fclose(file);
  return matrix;
}

void copy_matrix(int **src, int **dest, int rows, int cols) {
  for (int i = 0; i < rows; i++)
    for (int j = 0; j < cols; j++)
      dest[i][j] = src[i][j];
}

int main(int argc, char const *argv[]) {
  if (argc < 4) {
    fprintf(stderr, "Uso: %s <n_hijos> <iteraciones> <archivo>\n", argv[0]);
    exit(EXIT_FAILURE);
  }

  int n_children = atoi(argv[1]);
  int iterations = atoi(argv[2]);
  const char *filename = argv[3];

  int rows, cols;
  int **temp_matrix = read_file(filename, &rows, &cols);

  size_t size_mat = sizeof_dm(rows, cols, sizeof(int));
  int shm_id_l = shmget(IPC_PRIVATE, size_mat, IPC_CREAT | 0600);
  int **mat_l = shmat(shm_id_l, NULL, 0);
  create_index((void *)mat_l, rows, cols, sizeof(int));

  int shm_id_e = shmget(IPC_PRIVATE, size_mat, IPC_CREAT | 0600);
  int **mat_e = shmat(shm_id_e, NULL, 0);
  create_index((void *)mat_e, rows, cols, sizeof(int));

  int shm_id_sem =
      shmget(IPC_PRIVATE, sizeof(sem_t) * n_children * 2, IPC_CREAT | 0600);
  sem_t *sems = shmat(shm_id_sem, NULL, 0);

  for (int i = 0; i < n_children; i++) {
    sem_init(&sems[i * 2], 1, 0);     // padre a hijo
    sem_init(&sems[i * 2 + 1], 1, 0); // hijo a padre
  }

  copy_matrix(temp_matrix, mat_l, rows, cols);
  for (int i = 0; i < rows; i++)
    free(temp_matrix[i]);
  free(temp_matrix);

  int chunk = rows / n_children;

  for (int i = 0; i < n_children; i++) {
    pid_t pid = fork();
    if (pid == 0) {
      int child_index = i;
      int start = i * chunk;
      int end = (i == n_children - 1) ? rows : start + chunk;

      for (int t = 0; t < iterations; t++) {
        sem_wait(&sems[child_index * 2]);
        for (int i = start; i < end; i++) {
          for (int j = 0; j < cols; j++) {
            if (i == 0 || i == rows - 1 || j == 0 || j == cols - 1)
              continue;
            mat_e[i][j] = value(mat_l, i, j);
          }
        }

        sem_post(&sems[child_index * 2 + 1]);
      }

      shmdt(mat_l);
      shmdt(mat_e);
      shmdt(sems);
      exit(EXIT_SUCCESS);
    }
  }

  for (int t = 0; t < iterations; t++) {
    for (int i = 0; i < n_children; i++)
      sem_post(&sems[i * 2]);

    for (int i = 0; i < n_children; i++)
      sem_wait(&sems[i * 2 + 1]);

    printf("Paso %d:\n", t + 1);
    for (int i = 0; i < rows; i++) {
      for (int j = 0; j < cols; j++)
        printf("%3d", mat_l[i][j]);
      printf("\n");
    }
    printf("\n");

    for (int i = 1; i < rows - 1; i++)
      for (int j = 1; j < cols - 1; j++)
        mat_l[i][j] = mat_e[i][j];
  }

  for (int i = 0; i < n_children; i++)
    wait(NULL);

  for (int i = 0; i < n_children * 2; i++)
    sem_destroy(&sems[i]);

  shmdt(mat_l);
  shmctl(shm_id_l, IPC_RMID, NULL);
  shmdt(mat_e);
  shmctl(shm_id_e, IPC_RMID, NULL);
  shmdt(sems);
  shmctl(shm_id_sem, IPC_RMID, NULL);

  return EXIT_SUCCESS;
}
