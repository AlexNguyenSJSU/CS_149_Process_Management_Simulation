

#ifndef FINAL_TRIAL_DATASTRUCTS_H
#define FINAL_TRIAL_DATASTRUCTS_H
#include <string>
#include <vector>
#include <iostream>


struct Instruction
{
    char operation;
    int intArg;
    std::string stringArg;
};

class ProcessInfo{
public:
    int processId;
    unsigned int priority;

    // Overload the operator < for comparison in min heap
    bool operator < (const ProcessInfo& other) const
    {
        return priority > other.priority;
    }

    // Overload the operator << for printing with cout
    friend std::ostream& operator << (std::ostream& os, const ProcessInfo& processInfo)
    {
        os << "ProcessId: " << processInfo.processId << ", Priority: " << processInfo.priority;
        return os;
    }
};

struct Cpu
{
    std::vector<Instruction> *pProgram;
    unsigned int programCounter;
    int value;
    unsigned int timeSlice;
    unsigned int timeSliceUsed;
};

enum class State // using scoped enum since it provides better encapsulation
{
    Ready,
    Running,
    Blocked
};

struct PCB
{
    int processId;
    int parentProcessId;
    std::vector<Instruction> program;
    unsigned int programCounter;
    int value;
    unsigned int priority;
    State state;
    unsigned int startTime;
    unsigned int timeUsed;
};
#endif
