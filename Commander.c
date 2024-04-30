#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>

int main() {
    int pipefd[2];  // Array to hold read and write file descriptors
    pid_t pid;  // Process ID for fork()

    // Create the pipe
    if (pipe(pipefd) == -1) {
        perror("pipe");
        exit(EXIT_FAILURE);
    }

    // Fork to create the Process Manager
    pid = fork();
    if (pid == -1) {
        perror("fork");
        exit(EXIT_FAILURE);
    }

    if (pid == 0) {
        // Child process (Process Manager)
        close(pipefd[1]);  // Close write end in child process
        char buffer[100];  // Buffer to read commands

        // Process Manager logic
        while (1) {
            ssize_t num_bytes = read(pipefd[0], buffer, sizeof(buffer) - 1);
            if (num_bytes > 0) {
                buffer[num_bytes] = '\0';  // Null-terminate the string
                char *command = strtok(buffer, "\n");
                while (command) {
                    if (strcmp(command, "T") == 0) {
                        printf("Process Manager received 'T', terminating.\n");
                        exit(EXIT_SUCCESS);
                    } else {
                        printf("Process Manager received command: %s\n", command);
                    }
                    command = strtok(NULL, "\n");
                }
            } else {
                // Wait for data to arrive
                sleep(1);
            }
        }
    } else {
        // Parent process (Commander)
        close(pipefd[0]);  // Close read end in the parent process
        char command[10];  // Buffer to read input commands

        // Commander logic
        while (1) {
            printf("Enter command (Q, U, P, T): ");
            fflush(stdout);

            if (fgets(command, sizeof(command), stdin)) {
                // Get the newline character from input
                command[strcspn(command, "\n")] = '\0';

                // Write command to the pipe
                write(pipefd[1], command, strlen(command) + 1);

                if (strcmp(command, "T") == 0) {
                    printf("Commander sent 'T', terminating.\n");
                    break;
                }

                // Simulate delay between command inputs
                sleep(1);
            } else {
                perror("fgets");
                break;
            }
        }

        close(pipefd[1]);  // Close the write end in the parent process

        // Wait for the child process to exit
        wait(NULL);
    }

    return 0;
}
