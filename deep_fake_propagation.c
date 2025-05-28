#include <semaphore.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <time.h>

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

    int **matrix_alloc = calloc(*rows, sizeof(int *));
    for (int i = 0; i < *rows; ++i) {
        matrix_alloc[i] = calloc(*cols, sizeof(int));
    }

    for (int i = 0; i < *rows; ++i)
        for (int j = 0; j < *cols; ++j)
            fscanf(file, "%d", &matrix_alloc[i][j]);

    fclose(file);
    return matrix_alloc;
}

int value_0_to_1(int **input_matrix, int x, int y, int rows, int cols) {
    int dx[] = {-1, -1, -1, 0, 0, 1, 1, 1};
    int dy[] = {-1, 0, 1, -1, 1, -1, 0, 1};

    int count = 0;
    for (int i = 0; i < N_NEIGHBORS; ++i) {
        int nx = x + dx[i];
        int ny = y + dy[i];
        if (nx >= 0 && ny >= 0 && nx < rows && ny < cols && input_matrix[nx][ny] > 0)
            count++;
    }

    if (input_matrix[x][y] == 0 && count >= 2)
        return 1;
    return input_matrix[x][y];
}

int value_1_to_2(int **input_matrix, int x, int y, int rows, int cols) {
    if (input_matrix[x][y] != 1)
        return input_matrix[x][y];

    int dx[] = {-1, -1, -1, 0, 0, 1, 1, 1};
    int dy[] = {-1, 0, 1, -1, 1, -1, 0, 1};
    int counter2 = 0;

    for (int i = 0; i < N_NEIGHBORS; ++i) {
        int nx = x + dx[i];
        int ny = y + dy[i];
        if (nx >= 0 && ny >= 0 && nx < rows && ny < cols) {
            if (input_matrix[nx][ny] == 2)
                counter2++;
        }
    }

    double P = 0.15 + (counter2 * 0.05);
    double r = (double)rand() / RAND_MAX;
    return (P > r) ? 2 : 1;
}

int main(int argc, char *argv[]) {
    srand(time(NULL));

    if (argc < 3)
        error("invalid parameters: <file> <shifts>");

    int n_childs = 2;
    const char *filename = argv[1];
    int shifts = atoi(argv[2]);
    
    int rows, cols;
    int **initial_matrix = read_file(filename, &rows, &cols);

    size_t size_mat = rows * cols * sizeof(int);

    int shm_id_r = shmget(IPC_PRIVATE, size_mat, 0666 | IPC_CREAT);
    int *shm_r_ptr = (int *)shmat(shm_id_r, NULL, 0);
    int **mat_r = malloc(rows * sizeof(int *));

    int shm_id_w = shmget(IPC_PRIVATE, size_mat, 0666 | IPC_CREAT);
    int *shm_w_ptr = (int *)shmat(shm_id_w, NULL, 0);
    int **mat_w = malloc(rows * sizeof(int *));

    int shm_id_buf = shmget(IPC_PRIVATE, size_mat, 0666 | IPC_CREAT);
    int *shm_buf_ptr = (int *)shmat(shm_id_buf, NULL, 0);
    int **mat_buf = malloc(rows * sizeof(int *));


    for (int i = 0; i < rows; ++i) {
        mat_r[i] = &shm_r_ptr[i * cols];
        mat_w[i] = &shm_w_ptr[i * cols];
        mat_buf[i] = &shm_buf_ptr[i * cols];
    }

    for (int i = 0; i < rows; ++i)
        for (int j = 0; j < cols; ++j)
            mat_r[i][j] = initial_matrix[i][j];

    for (int i = 0; i < rows; ++i)
        free(initial_matrix[i]);
    free(initial_matrix);

    int shm_id_sem = shmget(IPC_PRIVATE, sizeof(sem_t) * n_childs * 2, 0666 | IPC_CREAT);
    sem_t *sems = shmat(shm_id_sem, NULL, 0);

    sem_init(&sems[0], 1, 0);
    sem_init(&sems[1], 1, 0);
    sem_init(&sems[2], 1, 0);
    sem_init(&sems[3], 1, 0);

    pid_t pid0 = fork();
    if (pid0 == 0) {
        for (int t = 0; t < shifts; ++t) {
            sem_wait(&sems[0]);
            
            for (int x = 0; x < rows; ++x) {
                for (int y = 0; y < cols; ++y) {
                    mat_buf[x][y] = value_0_to_1(mat_r, x, y, rows, cols);
                }
            }
            
            sem_post(&sems[1]);
        }
        shmdt(shm_r_ptr);
        shmdt(shm_w_ptr);
        shmdt(shm_buf_ptr);
        shmdt(sems);
        exit(EXIT_SUCCESS);
    }

    pid_t pid1 = fork();
    if (pid1 == 0) {
        for (int t = 0; t < shifts; ++t) {
            sem_wait(&sems[2]);

            for (int x = 0; x < rows; ++x) {
                for (int y = 0; y < cols; ++y) {
                    int val_from_0_to_1 = mat_buf[x][y];
                    int val_from_1_to_2 = value_1_to_2(mat_r, x, y, rows, cols);

                    if (val_from_0_to_1 == 1 && mat_r[x][y] == 0) {
                        mat_w[x][y] = 1;
                    } else if (mat_r[x][y] == 1) {
                        mat_w[x][y] = val_from_1_to_2;
                    } else {
                        mat_w[x][y] = mat_r[x][y];
                    }
                }
            }
            
            sem_post(&sems[3]);
        }
        shmdt(shm_r_ptr);
        shmdt(shm_w_ptr);
        shmdt(shm_buf_ptr);
        shmdt(sems);
        exit(EXIT_SUCCESS);
    }

    for (int t = 0; t < shifts; ++t) {
        sem_post(&sems[0]);
        sem_wait(&sems[1]);

        sem_post(&sems[2]);
        sem_wait(&sems[3]);

        printf("Iteracion %d:\n", t + 1);
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

    wait(NULL); 
    wait(NULL);

    for (int i = 0; i < n_childs * 2; ++i)
        sem_destroy(&sems[i]);

    shmdt(shm_r_ptr);
    shmdt(shm_w_ptr);
    shmdt(shm_buf_ptr);
    shmdt(sems);

    free(mat_r);
    free(mat_w);
    free(mat_buf);

    shmctl(shm_id_r, IPC_RMID, NULL);
    shmctl(shm_id_w, IPC_RMID, NULL);
    shmctl(shm_id_buf, IPC_RMID, NULL);
    shmctl(shm_id_sem, IPC_RMID, NULL);

    return 0;
}
