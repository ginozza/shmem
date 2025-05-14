#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <time.h>

int main() {
    int rows, cols, num_children;
    srand(time(NULL));

    printf("Enter matrix size (rows m, columns n): ");
    scanf("%d %d", &rows, &cols);

    printf("Enter number of child processes: ");
    scanf("%d", &num_children);

    int shm_matrix = shmget(IPC_PRIVATE, sizeof(int) * rows * cols, IPC_CREAT | 0666);
    int shm_vector = shmget(IPC_PRIVATE, sizeof(int) * cols, IPC_CREAT | 0666);
    int shm_result = shmget(IPC_PRIVATE, sizeof(int) * rows, IPC_CREAT | 0666);
    int shm_index = shmget(IPC_PRIVATE, sizeof(int), IPC_CREAT | 0666);

    int (*matrix)[cols] = shmat(shm_matrix, NULL, 0);
    int *vector = shmat(shm_vector, NULL, 0);
    int *result = shmat(shm_result, NULL, 0);
    int *current_index = shmat(shm_index, NULL, 0);

    *current_index = 0;

    for (int i = 0; i < rows; i++)
        for (int j = 0; j < cols; j++)
            matrix[i][j] = rand() % 10;

    for (int j = 0; j < cols; j++)
        vector[j] = rand() % 10;

    printf("Matrix:\n");
    for (int i = 0; i < rows; i++) {
        for (int j = 0; j < cols; j++) printf("%d ", matrix[i][j]);
        printf("\n");
    }

    printf("Vector:\n");
    for (int i = 0; i < cols; i++) printf("%d ", vector[i]);
    printf("\n");

    for (int h = 0; h < num_children; h++) {
        pid_t pid = fork();
        if (pid == 0) {
            while (1) {
                int row;
                row = __sync_fetch_and_add(current_index, 1);
                if (row >= rows) break;

                int sum = 0;
                for (int j = 0; j < cols; j++)
                    sum += matrix[row][j] * vector[j];

                result[row] = sum;
            }
            shmdt(matrix); shmdt(vector); shmdt(result); shmdt(current_index);
            exit(0);
        }
    }

    for (int i = 0; i < num_children; i++) wait(NULL);

    printf("Result A * B:\n");
    for (int i = 0; i < rows; i++) printf("%d\n", result[i]);

    shmdt(matrix); shmdt(vector); shmdt(result); shmdt(current_index);
    shmctl(shm_matrix, IPC_RMID, NULL);
    shmctl(shm_vector, IPC_RMID, NULL);
    shmctl(shm_result, IPC_RMID, NULL);
    shmctl(shm_index, IPC_RMID, NULL);

    return 0;
}
