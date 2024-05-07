int runProcessManager(int fileDescriptor) {
    // Initialization of the first process
    if (!createProgram("init", pcbEntry[0].program)) {
        return EXIT_FAILURE;
    }
    // Set initial values for the first process
    pcbEntry[0].processId = 0;
    pcbEntry[0].parentProcessId = -1;
    pcbEntry[0].programCounter = 0;
    pcbEntry[0].value = 0;
    pcbEntry[0].priority = 0;
    pcbEntry[0].state = STATE_RUNNING;
    pcbEntry[0].startTime = timestamp;
    pcbEntry[0].timeUsed = 0;
    runningState = 0;
    cpu.pProgram = &(pcbEntry[0].program);
    cpu.programCounter = pcbEntry[0].programCounter;
    cpu.value = pcbEntry[0].value;

    char command;
    // Command processing loop
    while (read(fileDescriptor, &command, sizeof(command)) == sizeof(command)) {
        switch (command) {
            case 'Q':
                quantum();
                break;
            case 'U':
                unblock();
                break;
            case 'P':
                print();
                break;
            case 'T':
                reportState();  // Assuming implementation to print final state
                return EXIT_SUCCESS;  // Terminate after handling 'T'
            default:
                cout << "Received an unrecognized command: " << command << endl;
        }
    }

    return EXIT_FAILURE;  // Return failure if read fails or loop exits unexpectedly
}
