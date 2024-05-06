#include <boost/algorithm/string.hpp> // for trimming the name of the file
#include <cctype> // for toupper()
#include <cstdlib> // for EXIT_SUCCESS and EXIT_FAILURE
#include <cstring> // for strerror()
#include <cerrno> // for errno
#include <deque> // for deque (used for ready and blocked queues)
#include <fstream> // for ifstream (used for reading simulated programs)
#include <iostream> // for cout, endl, and cin
#include <sstream> // for stringstream (used for parsing simulated programs)
#include <sys/wait.h> // for wait()
#include <unistd.h> // for pipe(), read(), write(), close(), fork(), and _exit()
#include <vector> // for vector (used for PCB table)

using namespace std;
using namespace boost::algorithm; // by this cmd to install boost libraries: sudo apt install libboost-all-dev
                                  // for using boost/algorithm/string.hpp header file

#define NUM_OF_PROCESSES 10

class Instruction {
public:
    char operation;
    int intArg;
    string stringArg;
};

class Cpu {
public:
    vector<Instruction> *pProgram;
    int programCounter;
    int value;
    int timeSlice;
    int timeSliceUsed;
};

enum State {
    STATE_READY,
    STATE_RUNNING,
    STATE_BLOCKED
};

class PcbEntry {
public:
    int processId;
    int parentProcessId;
    vector<Instruction> program;
    unsigned int programCounter;
    int value;
    unsigned int priority;
    State state;
    unsigned int startTime;
    unsigned int timeUsed;
};

//string trim(string trimmed_str);

// The table has PCBs for all current processes in the computer system:
//vector<PcbEntry*> processesTable;
PcbEntry pcbEntry[NUM_OF_PROCESSES];
unsigned int timestamp = 0;
Cpu cpu;

// For the states below, -1 indicates empty (since it is an invalid index).
int runningState = -1;
deque<int> readyState;
deque<int> blockedState;

// In this implementation, we'll never explicitly clear PCB entries and the index in
// the table will always be the process ID. These choices waste memory, but since this
// program is just a simulation it the easiest approach. Additionally, debugging is
// simpler since table slots and process IDs are never re-used.
double cumulativeTimeDiff = 0;
int numTerminatedProcesses = 0;

bool createProgram(const string &filename, vector<Instruction> &program) {
    ifstream file;
    int lineNum = 0;

    file.open(filename.c_str());

    if (!file.is_open()) {
        cout << "Error opening file " << filename << endl;
        return false;
    }

    while (file.good()) {
        string line;
        getline(file, line);

        trim(line);

        if (line.size() > 0) {
            Instruction instruction;
            instruction.operation = toupper(line[0]);
            //instruction.stringArg = trim(line.erase(0, 1));
            line.erase(0,1);
            trim(line);
	        instruction.stringArg = line;
            
            stringstream argStream(instruction.stringArg);
            switch (instruction.operation) {
                case 'S': // Integer argument.
                case 'A': // Integer argument.
                case 'D': // Integer argument.
                case 'F': // Integer argument.
                    if (!(argStream >> instruction.intArg)) {
                        cout << filename << ":" << lineNum
                             << " - Invalid integer argument "
                             << instruction.stringArg << " for "
                             << instruction.operation << " operation"
                             << endl;
                        file.close();
                        return false;
                    }
                    break;
                case 'B': // No argument.
                case 'E': // No argument.
                    break;
                case 'R': // String argument.
                    // Note that since the string is trimmed on both ends, filenames
                    // with leading or trailing whitespace (unlikely) will not work.
                    if (instruction.stringArg.size() == 0) {
                        cout << filename << ":" << lineNum << " -Missing string argument"
                             << endl;
                        file.close();
                        return false;
                    }
                    break;
                default:
                    cout << filename << ":" << lineNum << " - Invalid operation, "
                         << instruction.operation << endl;
                    file.close();
                    return false;
            }
            
            program.push_back(instruction);
        }

        lineNum++;
    }
    
    file.close();
    return true;
}

/**
 * Removes whitespace at the start and the end of the string
 * @param trimmed_str the string which needs to be trimmed.
 * @return the trimmed string without whitespaces at beginning and end of the string.
 */
//string trim(string trimmed_str)
//{
//    int front = trimmed_str.find_first_not_of(' ');
//    int rear = trimmed_str.find_last_not_of(' ');

//    if (rear == -1 || front == -1) {
//        return "";
//    }

//    return trimmed_str.substr(front, rear - front + 1);
//}

/**
 * Implements the S operation and 
 * Sets the CPU value to the passed-in value.
 * @param value CPU's value will be set to this value
*/
void set(int value) {
    cpu.value = value;
}

/**
 * Implements the A operation and 
 * Adds the passed-in value to the CPU value.
 * @param value the value which is used to increment CPU's value by this 'value'
*/
void add(int value) { 
    cpu.value += value;
}

/**
 * Implements the D operation and
 * Subtracts the integer value from the CPU value.
 * @param value the value which is used to decrease CPU's value by this 'value'
*/
void decrement(int value) {
    cpu.value -= value;
}

/**
 * Performs scheduling.
*/
void schedule() {
    // 1. Return if there is still a processing running (runningState != -1). 
    //    There is no need to schedule if a process is already running (at least until iLab 3)
    const int fixed_time_slice = 5;

    if (runningState != -1) {
        cpu.timeSliceUsed += 1;
        return;
    }

    // 2. Get a new process to run, if possible, from the ready queue.
    if (!readyState.empty()) {
        int nextProcess = readyState.front();
        readyState.pop_front();

        // 3. If we were able to get a new process to run:
        //     a. Mark the processing as running (update the new process's PCB state)
        pcbEntry[nextProcess].state = STATE_RUNNING;
        pcbEntry[nextProcess].timeUsed += 1;
        //     b. Update the CPU structure with the PCB entry details (program, program counter,
        //        value, etc.)
        cpu.pProgram = &pcbEntry[nextProcess].program;
        cpu.programCounter = pcbEntry[nextProcess].programCounter;
        cpu.value = pcbEntry[nextProcess].value;   
        cpu.timeSlice = fixed_time_slice;   
        cpu.timeSliceUsed = 0;   

        // variable 'runningState' updates to the current process's index
        runningState = nextProcess; 
    }
}

/**
 * Implements the B operation.
*/
void block() {
    // TODO: Implement
    if (runningState != -1) {
        // 1. Add the PCB index of the running process (stored in runningState) to the blocked queue.
        blockedState.push_back(runningState);

        // 2. Update the process's PCB entry
        //     a. Change the PCB's state to blocked.
        pcbEntry[runningState].state = STATE_BLOCKED;
        //     b. Store the CPU program counter in the PCB's program counter.
        pcbEntry[runningState].programCounter = cpu.programCounter;
        //     c. Store the CPU's value in the PCB's value.
        pcbEntry[runningState].value = cpu.value;

        // 3. Update the running state to -1 (basically mark no process as running). 
        //    Note that a new process will be chosen to run later (via the Q command code calling the schedule() function).
        runningState = -1;
    }
}

// Implements the E operation.
void end() {
    // TODO: Implement
    if (runningState != -1) {
        // 1. Get the PCB entry of the running process.
        PcbEntry& running_pro = pcbEntry[runningState];

        // 2. Update the cumulative time difference (increment it by timestamp + 1 - start time of the process).
        cumulativeTimeDiff = cumulativeTimeDiff + (timestamp + 1 - running_pro.startTime);

        // 3. Increment the number of terminated processes.
        numTerminatedProcesses++;

        // 4. Update the running state to -1 (basically mark no process as running). 
        //    Note that a new process will be chosen to run later (via the Q command code calling the schedule function).
        runningState = -1;
    } 
}

/**
 * Implements the F operation.
 * @param value
*/
void fork(int value) {
    // TODO: Implement
    // 1. Get a free PCB index (pcbTable.size())
    int free_PCB_index = -1;
    for (int i = 0; i < NUM_OF_PROCESSES; i++) {
        if (pcbEntry[i].processId == -1) {
            free_PCB_index = i;
            break;
        }
    }

    // 2. Get the PCB entry for the current running process.
    PcbEntry& parent_pro = pcbEntry[runningState];

    // 3. Ensure the passed-in value is not out of bounds.
    // 4. Populate the PCB entry obtained in #1
    //     a. Set the process ID to the PCB index obtained in #1.
    //     b. Set the parent process ID to the process ID of the running process 
    //        (use the running process's PCB entry to get this).
    //     c. Set the program counter to the cpu program counter.
    //     d. Set the value to the cpu value.
    //     e. Set the priority to the same as the parent process's priority.
    //     f. Set the state to the ready state.
    //     g. Set the start time to the current timestamp
    if((free_PCB_index != -1) && ((value >= 0) && (value < parent_pro.program.size())) ){
	    PcbEntry& child_pro = pcbEntry[free_PCB_index];
        child_pro.processId = free_PCB_index;
        child_pro.parentProcessId = parent_pro.processId;
        child_pro.programCounter = cpu.programCounter;
        child_pro.value = cpu.value;
        child_pro.priority = parent_pro.priority;
        child_pro.state = STATE_READY;
        child_pro.startTime = timestamp;
    }

    // 5. Add the pcb index to the ready queue
    readyState.push_back(free_PCB_index);

    // 6. Increment the cpu's program counter by the value read in #3
    cpu.programCounter += value;
}

/**
 * Implements the R operation.
 * @param argument the address of the string variable 'argument'
 */ 
void replace(string &argument) {
    // TODO: Implement
    // 1. Clear the CPU's program (cpu.pProgram->clear())
    cpu.pProgram->clear();

    // 2. Use createProgram() to read in the filename specified by argument into the CPU (*cpu.pProgram)
    bool successful_create_program = createProgram(argument, *cpu.pProgram);
    // a. Consider what to do if createProgram fails. [I printed an error, 
    //    incremented the cpu program counter and then returned.] 
    //    Note that createProgram can fail if the file could not be opened or did not exist.
    if (!successful_create_program)
    {
        cout << "Error occurred " << endl;
        cpu.programCounter += 1;
        return;
    }

    // 3. Set the program counter to 0.
    cpu.programCounter = 0;
}

// Implements the Q command.
void quantum() {
    Instruction instruction;
    cout << "In quantum ";
    if (runningState == -1) {
        cout << "No processes are running" << endl;
        ++timestamp;
        return;
    }

    if (cpu.programCounter < cpu.pProgram->size()) {
        instruction = (*cpu.pProgram)[cpu.programCounter];
        ++cpu.programCounter;
    } 
    else {
        cout << "End of program reached without E operation" << endl;
        instruction.operation = 'E';
    }
    
    switch (instruction.operation) {
        case 'S':
            set(instruction.intArg);
            cout << "instruction S " << instruction.intArg << endl;
            break;
        case 'A':
            add(instruction.intArg);
            cout << "instruction A " << instruction.intArg << endl;
            break;
        case 'D':
            decrement(instruction.intArg);
            break;
        case 'B':
            block();
            break;
        case 'E':
            end();
            break;
        case 'F':
            fork(instruction.intArg);
            break;
        case 'R':
            replace(instruction.stringArg);
            break;
    }

    ++timestamp;
    schedule();
}

/**
 * Implements the U command.
*/
void unblock() {
    // If the blocked queue contains any processes:
    if (!blockedState.empty()) {
        //  a. Remove a process form the front of the blocked queue.
        int next_process = blockedState.front();
        blockedState.pop_front();
        //  b. Add the process to the ready queue.
        readyState.push_back(next_process);
        //  c. Change the state of the process to ready (update its PCB entry).
        pcbEntry[next_process].state = STATE_READY;
        //  d. Call the schedule() function to give an unblocked process a chance to run (if possible).
        schedule();
    }
}

/**
 * Implements the helper function of print function below to execute P cmd.
 * @param state_name the passed name of state of the process which needs to be converted to string
*/
string helper_converting_state(State state_name) {
    if (state_name == STATE_READY) {
        return "READY";
    }
    else if (state_name == STATE_RUNNING) {
        return "RUNNING";
    }
    else if (state_name == STATE_BLOCKED) {
        return "BLOCKED";
    }
    else {
        return "UNIDENTIFIED!";
    }
}

/**
 * Implements the P command.
*/
void print() {
    //cout << "Print command is not implemented until iLab 3" << endl;
    cout << "" <<endl;
    cout << "***************************************************" << endl;
    cout << "The Current System State: \n";

    if (runningState != -1) {
        cout << "Current Running State(s): " << to_string(runningState) << endl;
    } 
    else {
        cout << "No State Running!" << endl;
    }

    cout << "-------------------------------" << endl;

    cout << "Process(es) in Ready Queue" << endl;
    for (int process: readyState){
        cout << "ID: " << process << endl;
    }

    cout << "-------------------------------" << endl;

    cout << "Process(es) in Blocked Queue" << endl;
    for (int process: blockedState) {
        cout << "ID: " << process << endl;
    }

    cout << "-------------------------------" << endl;
    cout << "Process Table" << endl;
    cout << "" <<endl;

    for (const auto& each_process: pcbEntry) {
        if (each_process.processId >= 0) {
            cout << "Process ID: " << each_process.processId << endl;
            cout << "Parent Process ID: " << each_process.parentProcessId << endl;
            cout << "Process Program Counter: " << each_process.programCounter << endl;
            cout << "Process Value: " << each_process.value << endl;
            cout << "Process Priority: " << each_process.priority << endl;
            cout << "Process State: " << helper_converting_state(each_process.state) << endl;
            
            cout << "Process Start: " << each_process.startTime << endl;
            cout << "Process timeUsed: " << each_process.timeUsed << endl;
            cout << "........................" << endl;
        }
    }

    cout << "***************************************************" << endl;
}

// Function that implements the process manager.
int runProcessManager(int fileDescriptor) {
    //vector<PcbEntry> pcbTable;
    // Attempt to create the init process.
    if (!createProgram("file.txt", pcbEntry[0].program)) {
        return EXIT_FAILURE;
    }

    pcbEntry[0].processId = 0;
    pcbEntry[0].parentProcessId = -1;
    pcbEntry[0].programCounter = 0;
    pcbEntry[0].value = 0;
    pcbEntry[0].priority = 0;
    pcbEntry[0].state = STATE_RUNNING;
    pcbEntry[0].startTime = 0;
    pcbEntry[0].timeUsed = 0;

    for (int i = 1; i < 10; ++i) {
        pcbEntry[i].processId = -1;
        pcbEntry[i].parentProcessId = -1;
        pcbEntry[i].programCounter = 0;
        pcbEntry[i].value = 0;
        pcbEntry[i].priority = 0;
        pcbEntry[i].state = STATE_READY;
        pcbEntry[i].startTime = 0;
        pcbEntry[i].timeUsed = 0;
    }

    runningState = 0;

    cpu.pProgram = &(pcbEntry[0].program);
    cpu.programCounter = pcbEntry[0].programCounter;
    cpu.value = pcbEntry[0].value;
    timestamp = 0;
    double avgTurnaroundTime = 0;

    // Loop until a 'T' is read, then terminate.
    char ch;
    do {
        // Read a command character from the pipe.
        if (read(fileDescriptor, &ch, sizeof(ch)) != sizeof(ch)) {
            // Assume the parent process exited, breaking the pipe.
            break;
        }

        //TODO: Write a switch statement
        switch (ch) {
            case 'Q':
                quantum();
                break;
            case 'U':
                cout << "You entered U" << endl;
                unblock();
                break;
            case 'P':
                cout << "You entered P" << endl;
                print();
                break;
            case 'T':
                cout << "Terminate!" << endl;
                break;
            default:
                cout << "You entered an invalid character!" << endl;
        }
    } while (ch != 'T');

    if (numTerminatedProcesses > 0) {
		double avg_turn_ard_time = (cumulativeTimeDiff)/(numTerminatedProcesses);
		cout << "Average Turnaround Time: " << avg_turn_ard_time << endl;
	}
	else {
		cout << "Terminated with nothing!" << endl;
	}

    return EXIT_SUCCESS;
}

int main(int argc, char *argv[]) {
    int pipeDescriptors[2];
    pid_t processMgrPid;
    char ch;
    int result;

    //TODO: Create a pipe
    if (pipe(pipeDescriptors) == -1) {
	    return 1;
    }

    //USE fork() SYSTEM CALL to create the child process and save the value returned in processMgrPid variable
    if((processMgrPid = fork()) == -1) exit(1); /* FORK FAILED */
    if (processMgrPid == 0) {
        // The process manager process is running.
        // Close the unused write end of the pipe for the process manager process.
        close(pipeDescriptors[1]);
        
        // Run the process manager.
        result = runProcessManager(pipeDescriptors[0]);

        // Close the read end of the pipe for the process manager process (for cleanup purposes).
        close(pipeDescriptors[0]);

        _exit(result);
    }
    else {
        // The commander process is running.
        // Close the unused read end of the pipe for the commander process.
        close(pipeDescriptors[0]);

        // Loop until a 'T' is written or until the pipe is broken.
        do {
            cout << "Enter Q, P, U or T" << endl;
            cout << "$ ";
            cin >> ch ;

            // Pass commands to the process manager process via the pipe.
            if (write(pipeDescriptors[1], &ch, sizeof(ch)) != sizeof(ch)) {
                // Assume the child process exited, breaking the pipe.
                break;
            }
        } while (ch != 'T');

        write(pipeDescriptors[1], &ch, sizeof(ch));

        // Close the write end of the pipe for the commander process (for cleanup purposes).
        close(pipeDescriptors[1]);

        // Wait for the process manager to exit.
        wait(&result);
    }
    
    return result;
}
