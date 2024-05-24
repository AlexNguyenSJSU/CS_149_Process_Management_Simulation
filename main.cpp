#include <cctype>
#include <cstdlib>
#include <cstring>
#include <cerrno>
#include <fstream>
#include <iostream>
#include <sstream>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>
#include <queue>

#include "datastructs.h"

// Define maximum and minimum process priorities
constexpr unsigned int MAX_PRIORITY = 9;
constexpr unsigned int MIN_PRIORITY = 0;

// Array mapping process priorities to their time slices
static const unsigned int priorityTimeSlices[MAX_PRIORITY + 1] = {25, 22, 19, 16, 13, 11, 9, 6, 3, 1};

// Global data structures and variables
std::vector<PCB*> processTable;
unsigned int currentTimestamp = 0;
int runningProcessIndex = -1;
double cumulativeTurnaroundTime = 0;
int numTerminatedProcesses = 0;
Cpu cpu;
std::priority_queue<ProcessInfo> readyQueue;
std::priority_queue<ProcessInfo> blockedQueue;

// Function prototypes
void cleanupProcesses();
inline void calculateAverageTurnaroundTime(double& avgTurnaroundTime);
inline std::string& trim(std::string &str);
bool loadProgram(std::vector<Instruction> &program, const std::string &filename);
inline void setCpuValue(int value);
inline void addCpuValue(int value);
inline void decrementCpuValue(int value);
bool createInitialProcess();
void dispatch(PCB* prevProcess, PCB*& newProcess);
void schedule();
void block();
void end();
void forkProcess(int value);
void replaceProgram(const std::string &filename);
void quantum();
void unblock();
void printProcessState();
int runProcessManager(int fileDescriptor);

// Overload << operator for State enum
inline std::ostream& operator<<(std::ostream& os, State state) {
    switch (state) {
        case State::Ready:   os << "Ready";   break;
        case State::Running:  os << "Running";  break;
        case State::Blocked:  os << "Blocked";  break;
        default:
            os.setstate(std::ios_base::failbit);
            break;
    }
    return os;
}

// Cleanup all processes
void cleanupProcesses() {
    for (PCB* process : processTable) {
        delete process;
    }
    processTable.clear();
}

// Calculate and return the average turnaround time
inline void calculateAverageTurnaroundTime(double& avgTurnaroundTime) {
    if (numTerminatedProcesses) {
        avgTurnaroundTime = cumulativeTurnaroundTime / numTerminatedProcesses;
    }
}

// Trim leading/trailing whitespace from a string (in-place)
inline std::string& trim(std::string &str) {
    str.erase(str.find_last_not_of(" \t\n\v\f\r") + 1);
    str.erase(0, str.find_first_not_of(" \t\n\v\f\r"));
    return str;
}

// Load a process's program from a file
bool loadProgram(std::vector<Instruction> &program, const std::string &filename) {
    std::ifstream file(filename);
    if (!file) {
        std::cerr << "Error opening file: " << filename << " in " << getcwd(nullptr, 0) << std::endl;
        return false;
    }

    program.clear();
    std::string line;
    int lineNumber = 0;
    while (std::getline(file, line)) {
        trim(line);
        if (!line.empty()) {
            Instruction instruction;
            instruction.operation = static_cast<char>(std::toupper(line[0]));
            instruction.stringArg = trim(line.erase(0, 1));

            switch (instruction.operation) {
                case 'S':
                case 'A':
                case 'D':
                case 'F': {
                    std::istringstream iss(instruction.stringArg);
                    if (!(iss >> instruction.intArg)) {
                        std::cerr << filename << ":" << lineNumber << " - Invalid integer argument: "
                                  << instruction.stringArg << " for operation " << instruction.operation << std::endl;
                        return false;
                    }
                    break;
                }
                case 'B':
                case 'E':
                    break;
                case 'R':
                    if (instruction.stringArg.empty()) {
                        std::cerr << filename << ":" << lineNumber << " - Missing string argument" << std::endl;
                        return false;
                    }
                    break;
                default:
                    std::cerr << filename << ":" << lineNumber << " - Invalid operation: " << instruction.operation << std::endl;
                    return false;
            }
            program.push_back(instruction);
        }
        lineNumber++;
    }
    return true;
}

// CPU Operations
inline void setCpuValue(int value) {
    cpu.value = value;
    std::cout << "Set CPU value to " << value << std::endl;
}

inline void addCpuValue(int value) {
    cpu.value += value;
    std::cout << "Incremented CPU value by " << value << std::endl;
}

inline void decrementCpuValue(int value) {
    cpu.value -= value;
    std::cout << "Decremented CPU value by " << value << std::endl;
}

// Create and initialize the initial process
bool createInitialProcess() {
    PCB* initialProcess = new PCB;
    if (!loadProgram(initialProcess->program, "init.txt")) {
        delete initialProcess;
        return false;
    }
    initialProcess->processId = 0;
    initialProcess->parentProcessId = -1;
    initialProcess->programCounter = 0;
    initialProcess->value = 0;
    initialProcess->priority = 0;
    initialProcess->state = State::Running;
    initialProcess->startTime = 0;
    initialProcess->timeUsed = 0;
    processTable.push_back(initialProcess);
    runningProcessIndex = 0;
    cpu.pProgram = &initialProcess->program;
    cpu.timeSlice = priorityTimeSlices[0];
    std::cout << "Running initial process, pid = 0" << std::endl;
    return true;
}

// Context Switch
void dispatch(PCB* prevProcess, PCB*& newProcess) {
    // If there was a previous process, update its state
    if (prevProcess) {
        prevProcess->value = cpu.value;
        prevProcess->priority = std::min(prevProcess->priority + 1, MAX_PRIORITY); // MAX_PRIORITY is now unsigned
        prevProcess->programCounter = cpu.programCounter;
        prevProcess->timeUsed += cpu.timeSliceUsed;
        prevProcess->state = State::Ready;
        readyQueue.push({prevProcess->processId, prevProcess->priority});
    }

    // Load the new process into the CPU
    newProcess->state = State::Running;
    runningProcessIndex = newProcess->processId;
    cpu.pProgram = &newProcess->program;
    cpu.programCounter = newProcess->programCounter;
    cpu.value = newProcess->value;
    cpu.timeSliceUsed = 0;
    cpu.timeSlice = priorityTimeSlices[newProcess->priority];
    std::cout << "Process running, pid = " << newProcess->processId << std::endl;
}

// Process Scheduler
void schedule() {
    if (readyQueue.empty()) {
        return;
    }

    PCB* mostReadyProcess = processTable[readyQueue.top().processId];

    // If no process is currently running, dispatch the most ready process
    if (runningProcessIndex == -1) {
        dispatch(nullptr, mostReadyProcess);
        std::cout << "Currently running process: " << runningProcessIndex << std::endl;
        return;
    }

    PCB* currentlyRunningProcess = processTable[runningProcessIndex];

    // Dispatch the most ready process if:
    //  - The current time slice has been used up
    //  - The most ready process has a higher priority than the currently running process
    if (cpu.timeSliceUsed >= cpu.timeSlice || mostReadyProcess->priority < currentlyRunningProcess->priority) {
        dispatch(currentlyRunningProcess, mostReadyProcess);
    } else {
        std::cout << "Currently running process: " << runningProcessIndex << std::endl;
    }
}

// Block the currently running process
void block() {
    PCB* currentProcess = processTable[runningProcessIndex];
    currentProcess->priority = std::max(currentProcess->priority - 1, MIN_PRIORITY);
    blockedQueue.push({runningProcessIndex, currentProcess->priority});
    currentProcess->state = State::Blocked;
    currentProcess->programCounter = cpu.programCounter;
    currentProcess->value = cpu.value;
    currentProcess->timeUsed += cpu.timeSliceUsed;
    runningProcessIndex = -1;
    std::cout << "Blocked process, pid = " << currentProcess->processId << std::endl;
}

// End the currently running process
void end() {
    PCB* currentProcess = processTable[runningProcessIndex];
    cumulativeTurnaroundTime += currentTimestamp + 1 - currentProcess->startTime;
    numTerminatedProcesses++;
    std::cout << "Ended process, pid = " << currentProcess->processId << ". Value = " << cpu.value << std::endl;
    runningProcessIndex = -1;
}

// Fork a new process from the currently running process
void forkProcess(int value) {
    // Error handling for invalid fork value
    if (value < 0 || static_cast<size_t>(cpu.programCounter + value) >= cpu.pProgram->size()) {
        std::cerr << "Error: Invalid fork value, ending parent process." << std::endl;
        end();
        return;
    }

    int newProcessIndex = processTable.size();
    PCB* currentProcess = processTable[runningProcessIndex];
    PCB* newProcess = new PCB;

    // Copy parent process data to the new process
    *newProcess = *currentProcess;
    newProcess->processId = newProcessIndex;
    newProcess->programCounter = cpu.programCounter;
    newProcess->state = State::Ready;
    newProcess->startTime = currentTimestamp + 1;
    newProcess->timeUsed = 0;
    processTable.push_back(newProcess);
    readyQueue.push({newProcess->processId, newProcess->priority});
    std::cout << "Forked new process, pid = " << newProcess->processId << std::endl;
    cpu.programCounter += value;
}

// Replace the currently running process's program with a new one from a file
void replaceProgram(const std::string &filename) {
    if (!loadProgram(*cpu.pProgram, filename)) {
        std::cerr << "Error: Failed to replace program, ending process." << std::endl;
        end();
        return;
    }
    cpu.programCounter = 0;
    std::cout << "Replaced program of process with PID = " << runningProcessIndex << " with " << filename << std::endl;
}

// Simulate one time quantum
void quantum() {
    // If no process is currently running, increment the timestamp and return
    if (runningProcessIndex == -1) {
        std::cout << "No processes are running" << std::endl;
        currentTimestamp++;
        return;
    }

    Instruction instruction;

    // Fetch the next instruction from the currently running process
    if (cpu.programCounter < static_cast<int>(cpu.pProgram->size())) {
        instruction = (*cpu.pProgram)[cpu.programCounter++];
    } else {
        std::cerr << "Error: End of program reached without E operation" << std::endl;
        instruction.operation = 'E';
    }

    // Execute the fetched instruction
    switch (instruction.operation) {
        case 'S': setCpuValue(instruction.intArg); break;
        case 'A': addCpuValue(instruction.intArg); break;
        case 'D': decrementCpuValue(instruction.intArg); break;
        case 'B': block(); break;
        case 'E': end(); break;
        case 'F': forkProcess(instruction.intArg); break;
        case 'R': replaceProgram(instruction.stringArg); break;
    }

    // Increment timestamp and time slice used
    currentTimestamp++;
    cpu.timeSliceUsed++;

    // Call the scheduler to determine the next process to run
    schedule();
}

// Unblock the highest priority blocked process
void unblock() {
    if (blockedQueue.empty()) {
        return;
    }

    int processToUnblockIndex = blockedQueue.top().processId;
    PCB* processToUnblock = processTable[processToUnblockIndex];
    blockedQueue.pop();
    readyQueue.push({processToUnblock->processId, processToUnblock->priority});
    processToUnblock->state = State::Ready;
    std::cout << "Unblocked process, pid = " << processToUnblock->processId << std::endl;
    schedule();
}

// Print the current state of the process manager
void printProcessState() {
    pid_t pid = fork();

    if (pid == -1) {
        std::cerr << "fork: " << strerror(errno) << std::endl;
        return;
    }

    if (pid == 0) { // Child process
        printf("******************************************************************\n");
        printf("System State: \n");
        printf("******************************************************************\n");
        printf("CURRENT TIME: %d\n\n", currentTimestamp);

        if (runningProcessIndex != -1) {
            PCB* runningProcess = processTable[runningProcessIndex];
            printf("RUNNING PROCESS: \n"
                   "PID: %d\n"
                   "PPID: %d\n"
                   "State: %s\n"
                   "Priority: %u\n"
                   "Value: %d\n"
                   "Program Counter: %d\n"
                   "Start time: %u\n"
                   "Time used: %u\n",
                   runningProcess->processId,
                   runningProcess->parentProcessId,
                   (runningProcess->state == State::Ready ? "Ready" :
                    (runningProcess->state == State::Running ? "Running" : "Blocked")),
                   runningProcess->priority,
                   cpu.value,
                   cpu.programCounter,
                   runningProcess->startTime,
                   cpu.timeSliceUsed + runningProcess->timeUsed);
        }

        printf("\nBLOCKED PROCESSES: \n");
        std::priority_queue<ProcessInfo> tempBlockedQueue = blockedQueue;
        while (!tempBlockedQueue.empty()) {
            std::cout << tempBlockedQueue.top() << std::endl;
            tempBlockedQueue.pop();
        }

        printf("\nREADY PROCESSES: \n");
        std::priority_queue<ProcessInfo> tempReadyQueue = readyQueue;
        while (!tempReadyQueue.empty()) {
            std::cout << tempReadyQueue.top() << std::endl;
            tempReadyQueue.pop();
        }

        printf("******************************************************************\n");
        _exit(EXIT_SUCCESS);
    } else {
        wait(nullptr);
    }
}

// Run the process manager
int runProcessManager(int fileDescriptor) {
    if (!createInitialProcess()) {
        return EXIT_FAILURE;
    }

    double avgTurnaroundTime = 0;
    char commandChar;
    while (read(fileDescriptor, &commandChar, 1) == 1) {
        switch (std::toupper(commandChar)) {
            case 'Q': quantum(); break;
            case 'U': unblock(); break;
            case 'P': printProcessState(); break;
            case 'T':
                calculateAverageTurnaroundTime(avgTurnaroundTime);
                std::cout << "Average turnaround time: " << avgTurnaroundTime << std::endl;
                cleanupProcesses();
                return EXIT_SUCCESS;
            default:
                if (!std::isspace(commandChar)) {
                    std::cerr << "Unknown command: " << commandChar << std::endl;
                }
        }
    }

    cleanupProcesses();
    return EXIT_FAILURE;
}

// Main function (Commander process)
int main() {
    // Create a pipe for communication between the commander and the process manager
    int pipeDescriptors[2];
    if (pipe(pipeDescriptors) == -1) {
        std::cerr << "pipe: " << strerror(errno) << std::endl;
        return EXIT_FAILURE;
    }

    // Fork a child process to run the process manager
    pid_t processManagerPid = fork();
    if (processManagerPid == -1) {
        std::cerr << "fork: " << strerror(errno) << std::endl;
        return EXIT_FAILURE;
    }

    if (processManagerPid == 0) { // Child (Process Manager)
        close(pipeDescriptors[1]); // Close the write end of the pipe
        _exit(runProcessManager(pipeDescriptors[0])); // Run the process manager
    } else { // Parent (Commander)
        close(pipeDescriptors[0]); // Close the read end of the pipe
        char commandChar;
        do {
            std::cout << "Enter command (Q, P, U, T): ";
            std::cin >> commandChar;
            // Write the command to the process manager
            if (write(pipeDescriptors[1], &commandChar, 1) != 1) {
                std::cerr << "Error communicating with process manager." << std::endl;
                break;
            }
        } while (std::toupper(commandChar) != 'T'); // Continue until 'T' is entered

        // Wait for the child process to terminate
        wait(nullptr);
        close(pipeDescriptors[1]); // Close the write end of the pipe
    }

    return EXIT_SUCCESS;
}