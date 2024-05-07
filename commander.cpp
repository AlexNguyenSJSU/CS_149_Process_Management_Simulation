#include <iostream>
#include <unistd.h>
#include <sys/wait.h>
#include <cstring>
#include <cstdlib>

using namespace std;

// Function to run process manager
int runProcessManager(int read_fd);

int main() {
    int pipefd[2]; // Array to hold the file descriptors for the pipe
    pid_t pid;    // Process ID

    // Create a pipe
    if (pipe(pipefd) == -1) {
        cerr << "Failed to create pipe: " << strerror(errno) << endl;
        return EXIT_FAILURE;
    }

    // Fork a process
    pid = fork();
    if (pid == -1) {
        cerr << "Failed to fork process: " << strerror(errno) << endl;
        return EXIT_FAILURE;
    }

    if (pid == 0) {
        // Child process (Process Manager)
        close(pipefd[1]); // Close unused write end of the pipe
        int result = runProcessManager(pipefd[0]);
        close(pipefd[0]); // Close the read end of the pipe
        _exit(result);    // Use _exit in child to avoid flushing buffers
    } else {
        // Parent process (Commander)
        close(pipefd[0]); // Close unused read end
        char input;

        do {
            cout << "$ ";  // Prompt for input
            cin >> input;  // Read a single character command
            input = toupper(input); // Convert to uppercase to standardize

            // Write the command to the pipe
            if (write(pipefd[1], &input, 1) == -1) {
                cerr << "Failed to write to pipe: " << strerror(errno) << endl;
                return EXIT_FAILURE;
            }

        } while (input != 'T');

        close(pipefd[1]); // Close the write end
        wait(NULL);       // Wait for child process to finish
    }

    return EXIT_SUCCESS;
}

int runProcessManager(int read_fd) {
    char command;
    while (read(read_fd, &command, 1) > 0) {
        if (command == 'Q') {
            // Implement Quantum processing logic here
        } else if (command == 'U') {
            // Unblock first process logic
        } else if (command == 'P') {
            // Print system state logic
        } else if (command == 'T') {
            // Terminate system logic
            break;
        }
        // Add additional logic to handle commands as per the skeleton code's requirements
    }
    return EXIT_SUCCESS;
}
