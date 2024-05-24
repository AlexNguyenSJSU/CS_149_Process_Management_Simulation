#include <cctype>
#include <cstdlib>
#include <cstring>
#include <cerrno>
#include <deque>
#include <fstream>
#include <iostream>
#include <sstream>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>
#include <queue>
#include <unordered_map>
#include "datastructs.h"

#define MAX_PRIORITY 9
#define MIN_PRIORITY 0

// Array mapping process priorities to their time slices.
static const unsigned int priorityTimeSlices[MAX_PRIORITY + 1] = {25, 22, 19, 16, 13, 11, 9, 6, 3, 1};

// Global process table to store PCB information.
std::vector<PCB *> processTable;
// Current simulation time.
unsigned int currentTimestamp = 0;
// Index of the currently running process in the process table (-1 if none).
int runningProcessIndex = -1;
// Tracks the cumulative turnaround time.
double cumulativeTurnaroundTime = 0;
// Tracks the number of terminated processes.
int numTerminatedProcesses = 0;

// CPU object to simulate CPU operations.
Cpu cpu;
// Priority queue for ready processes.
std::priority_queue<ProcessInfo> readyQueue;
// Priority queue for blocked processes.
std::priority_queue<ProcessInfo> blockedQueue;

// Overload << operator for State enum class.
inline std::ostream& operator<<(std::ostream& os, State state) {
    switch (state) {
        case State::Ready:   os << "Ready";   break;
        case State::Running:  os << "Running";  break;
        case State::Blocked:  os << "Blocked";  break;
        default:             os.setstate(std::ios_base::failbit);
    }
    return os;
}

// Trims whitespace from the beginning and end of a string.
inline std::string& trim(std::string &inputArgument) {
    const std::string whitespace = " \t\n\v\f\r";
    size_t found = inputArgument.find_last_not_of(whitespace);
    if (found != std::string::npos) {
        inputArgument.erase(found + 1);
        inputArgument.erase(0, inputArgument.find_first_not_of(whitespace));
    } else {
        inputArgument.clear();
    }
    return inputArgument;
}

// Extracts a simulated process program from a file.
bool extractProgramFromFile(std::vector<Instruction> &program, const std::string &filename) {
    std::ifstream inputFile(filename.c_str());
    if (!inputFile.is_open()) {
        std::cerr << "Error opening file \"" << filename << "\" in \"" << getcwd(nullptr, 0) << "\"" << std::endl;
        return false;
    }

    program.clear();
    std::string currentLine;
    int lineNumber = 0;
    while (getline(inputFile, currentLine)) {
        trim(currentLine);
        if (!currentLine.empty()) {
            Instruction instruction;
            instruction.operation = static_cast<char>(toupper(currentLine[0]));
            instruction.stringArg = trim(currentLine.erase(0, 1));

            switch (instruction.operation) {
                case 'S': // Set CPU value
                case 'A': // Add to CPU value
                case 'D': // Decrement CPU value
                case 'F': { // Fork process
                    std::stringstream argStream(instruction.stringArg);
                    if (!(argStream >> instruction.intArg)) {
                        std::cerr << filename << ":" << lineNumber << " - Invalid integer argument " << instruction.stringArg << " for " << instruction.operation << " operation" << std::endl;
                        return false;
                    }
                    break;
                }
                case 'B': // Block process
                case 'E': // End process
                    break;
                case 'R': // Replace process program
                    if (instruction.stringArg.empty()) {
                        std::cerr << filename << ":" << lineNumber << " - Missing string argument" << std::endl;
                        return false;
                    }
                    break;
                default:
                    std::cerr << filename << ":" << lineNumber << " - Invalid operation, " << instruction.operation << std::endl;
                    return false;
            }
            program.push_back(instruction);
        }
        lineNumber++;
    }
    return true;
}

// Sets the CPU value.
inline void set(int value) {
    cpu.value = value;
    std::cout << "Set CPU value to " << value << std::endl;
}

// Adds a value to the CPU value.
inline void add(int value) {
    cpu.value += value;
    std::cout << "Incremented CPU value by " << value << std::endl;
}

// Decrements the CPU value.
inline void decrement(int value) {
    cpu.value -= value;
    std::cout << "Decremented CPU value by " << value << std::endl;
}

// Performs context switching between processes.
void dispatch(PCB *prevRunningProcess, PCB *& newRunningProcess) {
    readyQueue.pop();
    if (prevRunningProcess != nullptr) {
        // Update the PCB of the previously running process.
        prevRunningProcess->value = cpu.value;
        if (prevRunningProcess->priority < MAX_PRIORITY) {
            prevRunningProcess->priority++;
        }
        prevRunningProcess->programCounter = cpu.programCounter;
        prevRunningProcess->timeUsed += cpu.timeSliceUsed;
        prevRunningProcess->state = State::Ready;
        readyQueue.push({prevRunningProcess->processId, prevRunningProcess->priority});
    }

    // Update the PCB of the newly running process.
    newRunningProcess->state = State::Running;
    runningProcessIndex = newRunningProcess->processId;

    // Update the CPU with the information of the newly running process.
    cpu.pProgram = &newRunningProcess->program;
    cpu.programCounter = newRunningProcess->programCounter;
    cpu.value = newRunningProcess->value;
    cpu.timeSliceUsed = 0;
    cpu.timeSlice = priorityTimeSlices[newRunningProcess->priority];
    std::cout << "Process running, pid = " << newRunningProcess->processId << std::endl;
}

// Schedules the next process to run based on priority and time slice.
void schedule() {
    if (readyQueue.empty()) {
        return;
    }

    PCB* mostReadyProcess = processTable[readyQueue.top().processId];
    if (runningProcessIndex == -1) {
        // No process is currently running, dispatch the most ready process.
        dispatch(nullptr, mostReadyProcess);
        std::cout << "Currently running process: " << runningProcessIndex << std::endl;
        return;
    }

    PCB* currentlyRunningProcess = processTable[runningProcessIndex];
    // If the current process has used its time slice or a higher priority process is ready, switch processes.
    if (cpu.timeSliceUsed >= cpu.timeSlice || mostReadyProcess->priority < currentlyRunningProcess->priority) {
        dispatch(currentlyRunningProcess, mostReadyProcess);
    } else {
        std::cout << "Currently running process: " << runningProcessIndex << std::endl;
    }
}

// Blocks the currently running process.
void block() {
    PCB *previouslyRunningProcess = processTable[runningProcessIndex];
    if (previouslyRunningProcess->priority > MIN_PRIORITY) {
        previouslyRunningProcess->priority--;
    }
    // Add the process to the blocked queue.
    blockedQueue.push({runningProcessIndex, previouslyRunningProcess->priority});
    previouslyRunningProcess->state = State::Blocked;
    previouslyRunningProcess->programCounter = cpu.programCounter;
    previouslyRunningProcess->value = cpu.value;
    previouslyRunningProcess->timeUsed += cpu.timeSliceUsed;
    runningProcessIndex = -1;
    std::cout << "Blocked process, pid = " << previouslyRunningProcess->processId << std::endl;
}

// Ends the currently running process.
void end() {
    PCB *endedProcess = processTable[runningProcessIndex];
    // Update turnaround time statistics.
    cumulativeTurnaroundTime += (double)(currentTimestamp + 1 - endedProcess->startTime);
    numTerminatedProcesses++;
    std::cout << "Ended process, pid = " << endedProcess->processId << ". Value = " << cpu.value << std::endl;
    runningProcessIndex = -1;
}

// Forks a new process from the currently running process.
void forkProcess(int value) {
    if (value < 0 || static_cast<size_t>(cpu.programCounter + value) >= cpu.pProgram->size()) {
        std::cerr << "Error executing F operation, ending parent process" << std::endl;
        end();
        return;
    }

    int newProcessIndex = static_cast<int>(processTable.size());
    PCB *runningProcess = processTable[runningProcessIndex];
    PCB *newProcess = new PCB;
    newProcess->processId = newProcessIndex;
    newProcess->parentProcessId = runningProcess->processId;
    newProcess->program = runningProcess->program;
    newProcess->programCounter = cpu.programCounter;
    newProcess->value = cpu.value;
    newProcess->priority = runningProcess->priority;
    newProcess->state = State::Ready;
    newProcess->startTime = currentTimestamp + 1;
    newProcess->timeUsed = 0;
    processTable.push_back(newProcess);
    readyQueue.push({newProcess->processId, newProcess->priority});
    std::cout << "Forked new process, pid = " << newProcess->processId << std::endl;
    cpu.programCounter += value;
}

// Replaces the program of the currently running process.
void replaceProgram(const std::string &fileName) {
    if (!extractProgramFromFile(*cpu.pProgram, fileName)) {
        std::cerr << "Error executing R operation, ending process" << std::endl;
        end();
        return;
    }
    cpu.programCounter = 0;
    std::cout << "Replaced program of process with PID = " << runningProcessIndex << " with " << fileName << std::endl;
}

// Simulates a single quantum of time in the process manager.
void quantum() {
    if (runningProcessIndex == -1) {
        std::cout << "No processes are running" << std::endl;
        currentTimestamp++;
        return;
    }

    Instruction instruction;
    if (cpu.programCounter < static_cast<int>(cpu.pProgram->size())) {
        instruction = (*cpu.pProgram)[cpu.programCounter];
        cpu.programCounter++;
    } else {
        std::cerr << "End of program reached without E operation" << std::endl;
        instruction.operation = 'E';
    }

    // Execute the current instruction.
    switch (instruction.operation) {
        case 'S': set(instruction.intArg); break;
        case 'A': add(instruction.intArg); break;
        case 'D': decrement(instruction.intArg); break;
        case 'B': block(); break;
        case 'E': end(); break;
        case 'F': forkProcess(instruction.intArg); break;
        case 'R': replaceProgram(instruction.stringArg); break;
        default: break;
    }

    currentTimestamp++;
    cpu.timeSliceUsed++;
    schedule();
}

// Unblocks the highest priority blocked process.
void unblock() {
    if (blockedQueue.empty()) {
        return;
    }

    int processToUnblockIndex = blockedQueue.top().processId;
    PCB *processToUnblock = processTable[processToUnblockIndex];
    blockedQueue.pop();
    readyQueue.push({processToUnblock->processId, processToUnblock->priority});
    processToUnblock->state = State::Ready;
    std::cout << "Unblocked process, pid = " << processToUnblock->processId << std::endl;

    schedule();
}

// Prints the current state of the process manager.
void printProcessState() {
    pid_t pid = fork();
    if (pid == -1) {
        std::cerr << "fork: " << strerror(errno) << std::endl;
        return;
    }

    if (pid == 0) {
        // Child process prints the system state.
        printf("******************************************************************\n");
        printf("The current system state is as follows: \n");
        printf("******************************************************************\n");

        printf("CURRENT TIME: %d\n\n", currentTimestamp);

        if (runningProcessIndex != -1) {
            PCB *runningProcess = processTable[runningProcessIndex];
            printf("RUNNING PROCESS: \n");
            std::cout << "PID: " << runningProcess->processId << '\n';
            std::cout << "PPID: " << runningProcess->parentProcessId << '\n';
            std::cout << "State: " << runningProcess->state << '\n';
            std::cout << "Priority: " << runningProcess->priority << '\n';
            std::cout << "Value: " << cpu.value << '\n';
            std::cout << "Program Counter: " << cpu.programCounter << '\n';
            std::cout << "Start time: " << runningProcess->startTime << '\n';
            std::cout << "Time used: " << (cpu.timeSliceUsed + runningProcess->timeUsed) << '\n';
        }

        printf("\nBLOCKED PROCESSES: \n");
        std::priority_queue<ProcessInfo> copyOfBlockedQueue = blockedQueue;
        while (!copyOfBlockedQueue.empty()) {
            ProcessInfo blockedProcessInfo = copyOfBlockedQueue.top();
            copyOfBlockedQueue.pop();
            std::cout << blockedProcessInfo << std::endl;
        }

        printf("\nPROCESSES READY TO EXECUTE: \n");
        std::priority_queue<ProcessInfo> copyOfReadyQueue = readyQueue;
        while (!copyOfReadyQueue.empty()) {
            ProcessInfo readyProcessInfo = copyOfReadyQueue.top();
            copyOfReadyQueue.pop();
            std::cout << readyProcessInfo << std::endl;
        }

        printf("******************************************************************\n");
        _exit(EXIT_SUCCESS);
    } else {
        // Parent process waits for the child to finish.
        wait(nullptr);
    }
}

// Creates the initial simulated process from "init.txt".
bool createInitialSimulatedProcess() {
    PCB *initialProcess = new PCB;
    if (!extractProgramFromFile(initialProcess->program, "init.txt")) {
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
    runningProcessIndex = initialProcess->processId;

    cpu.pProgram = &(initialProcess->program);
    cpu.programCounter = 0;
    cpu.value = 0;
    cpu.timeSlice = priorityTimeSlices[0];
    cpu.timeSliceUsed = 0;

    currentTimestamp = 0;
    std::cout << "Running initial process, pid = " << initialProcess->processId << std::endl;
    return true;
}

// Clears the process table and frees allocated memory.
void clearProcessTable() {
    for (auto &process : processTable) {
        delete process;
    }
    processTable.clear();
}

// Calculates the average turnaround time of terminated processes.
inline void calculateAverageTurnaroundTime(double &avgTurnaroundTime) {
    if (numTerminatedProcesses != 0) {
        avgTurnaroundTime = cumulativeTurnaroundTime / static_cast<double>(numTerminatedProcesses);
    }
}

// Runs the process manager simulation.
int runProcessManager(int fileDescriptor) {
    if (!createInitialSimulatedProcess()) {
        return EXIT_FAILURE;
    }

    double avgTurnaroundTime = 0.0;
    char commandChar;
    do {
        if (read(fileDescriptor, &commandChar, sizeof(commandChar)) != sizeof(commandChar)) {
            break;
        }

        if (isspace(commandChar)) {
            continue;
        }

        commandChar = static_cast<char>(toupper(commandChar));

        switch (commandChar) {
            case 'Q': // Simulate one quantum of time.
                quantum(); break;
            case 'U': // Unblock a process.
                unblock(); break;
            case 'P': // Print system state.
                printProcessState(); break;
            case 'T': // Calculate and print average turnaround time, then exit.
                calculateAverageTurnaroundTime(avgTurnaroundTime);
                std::cout << "The average turnaround time is " << avgTurnaroundTime << "." << std::endl;
                break;
            default:
                std::cerr << "Unknown command, " << commandChar << std::endl;
                break;
        }
    } while (commandChar != 'T');

    clearProcessTable();
    return EXIT_SUCCESS;
}

// Main function, acts as the commander process.
int main() {
    int pipeDescriptors[2];
    pid_t processManagerPid;
    char commandChar;
    int result;

    // Create a pipe for communication.
    if (pipe(pipeDescriptors) == -1) {
        std::cerr << "pipe: " << strerror(errno) << std::endl;
        return EXIT_FAILURE;
    }

    // Fork the process manager.
    processManagerPid = fork();
    if (processManagerPid == -1) {
        std::cerr << "fork: " << strerror(errno) << std::endl;
        return EXIT_FAILURE;
    }

    if (processManagerPid == 0) {
        // Process manager process.
        close(pipeDescriptors[1]);
        result = runProcessManager(pipeDescriptors[0]);
        close(pipeDescriptors[0]);
        _exit(result);
    } else {
        // Commander process.
        close(pipeDescriptors[0]);

        do {
            // Get command from the user.
            std::cout << "Enter Q, P, U or T" << std::endl;
            std::cout << "$ ";
            std::cin >> commandChar;

            // Send command to the process manager.
            if (write(pipeDescriptors[1], &commandChar, sizeof(commandChar)) != sizeof(commandChar)) {
                break;
            }
        } while (commandChar != 'T');

        wait(&result);
        close(pipeDescriptors[1]);
    }

    return result;
}