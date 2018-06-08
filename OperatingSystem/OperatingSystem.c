#include "OperatingSystem.h"
#include "OperatingSystemBase.h"
#include "MMU.h"
#include "Processor.h"
#include "Buses.h"
#include "Heap.h"
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include <time.h>

// Functions prototypes
void OperatingSystem_CreateDaemons();
void OperatingSystem_PCBInitialization(int, int, int, int,int);
void OperatingSystem_MoveToTheREADYState(int);
void OperatingSystem_PrintReadyToRunQueue();
void OperatingSystem_Dispatch(int);
void OperatingSystem_RestoreContext(int);
void OperatingSystem_SaveContext(int);
void OperatingSystem_TerminateProcess();
int OperatingSystem_LongTermScheduler();
void OperatingSystem_PreemptRunningProcess();
int OperatingSystem_CreateProcess(USER_PROGRAMS_DATA,int);
int OperatingSystem_ObtainMainMemory(int, int);
int OperatingSystem_ShortTermScheduler();
int OperatingSystem_ExtractFromReadyToRun(int);
void OperatingSystem_HandleException();
void OperatingSystem_HandleSystemCall();
void OperatingSystem_HandleYield();
void OperatingSystem_HandleClockInterrupt();
void OperatingSystem_MoveToTheBLOCKEDState(int);
int OperatingSystem_ExtractFromBLOCKED();
int OperatingSystem_ExecutingProcessIsMostPriorityProcess();
void OperatingSystem_PrintStatus();
int OperatingSystem_GetExecutingProcessID();
void OperatingSystem_ReleaseMainMemory(int);
void OperatingSystem_SearchForBestPartition(int);

// Foreign methods
int Processor_GetAccumulator();
int Processor_GetRegisterB();

// Exercise 5-b of V2
// Heap with blocked porcesses sort by when to wakeup
int sleepingProcessesQueue[PROCESSTABLEMAXSIZE];
int numberOfSleepingProcesses=0;

//Differents states from proccess
char * statesNames [5]={"NEW","READY","EXECUTING","BLOCKED","EXIT"};

// The process table
PCB processTable[PROCESSTABLEMAXSIZE];

// Address base for OS code
int OS_address_base = PROCESSTABLEMAXSIZE * MAINMEMORYSECTIONSIZE;
// Identifier of the current executing process
int executingProcessID=NOPROCESS;

// Identifier of the System Idle Process
int sipID;

// Array that contains the identifiers of the READY processes
int readyToRunQueue[NUMBEROFQUEUES][PROCESSTABLEMAXSIZE];
int numberOfReadyToRunProcesses[NUMBEROFQUEUES];

// Variable containing the number of not terminated user processes
int numberOfNotTerminatedUserProcesses=0;

// Variable containing the number of clock interruptions
int numberOfClockInterrupts=0;

// Initial set of tasks of the OS
void OperatingSystem_Initialize() {
	
	int i, selectedProcess;
	FILE *programFile; // For load Operating System Code

	// Obtain the memory requirements of the program
	int processSize=OperatingSystem_ObtainProgramSize(&programFile, "OperatingSystemCode");

	// Load Operating System Code
	OperatingSystem_LoadProgram(programFile, OS_address_base, processSize);
	
	// Initialize random feed
	srand(time(NULL));

	// Process table initialization (all entries are free)
	for (i=0; i<PROCESSTABLEMAXSIZE;i++)
		processTable[i].busy=0;
	
	// Initialization of the interrupt vector table of the processor
	Processor_InitializeInterruptVectorTable(OS_address_base);
	
	OperatingSystem_InitializePartitionTable();
	
	// Create all system daemon processes
	OperatingSystem_CreateDaemons();
	
	if (sipID<0) {
		// Show message "ERROR: Missing SIP program!\n"			 
		ComputerSystem_DebugMessage(21,SHUTDOWN);
		exit(1);
	}
	
	OperatingSystem_PrintStatus();
	
	// Create all user processes from the information given in the command line
	int aux = OperatingSystem_LongTermScheduler();
	
	if(OperatingSystem_IsThereANewProgram()==-1 && aux<=0) 
		OperatingSystem_ReadyToShutdown();
	
	// At least, one user process has been created
	// Select the first process that is going to use the processor
	selectedProcess=OperatingSystem_ShortTermScheduler();

	// Assign the processor to the selected process
	OperatingSystem_Dispatch(selectedProcess);

	// Initial operation for Operating System
	Processor_SetPC(OS_address_base);
}


// Daemon processes are system processes, that is, they work together with the OS.
// The System Idle Process uses the CPU whenever a user process is able to use it
void OperatingSystem_CreateDaemons() {
  
	USER_PROGRAMS_DATA systemIdleProcess;
	
	systemIdleProcess.executableName="SystemIdleProcess";
	sipID=OperatingSystem_CreateProcess(systemIdleProcess, DAEMONSQUEUE); 
	processTable[sipID].copyOfPCRegister=processTable[sipID].initialPhysicalAddress;
	processTable[sipID].copyOfPSWRegister=Processor_GetPSW();
}

// The LTS is responsible of the admission of new processes in the system.
// Initially, it creates a process from each program specified in the command line
int OperatingSystem_LongTermScheduler() {
  
	int PID,numberOfSuccessfullyCreatedProcesses=0;
	int index=0;
	
	while(OperatingSystem_IsThereANewProgram()==1){
		index = Heap_poll(arrivalTimeQueue,QUEUE_ARRIVAL, &numberOfProgramsInArrivalTimeQueue);
		PID=OperatingSystem_CreateProcess(*userProgramsList[index], USERPROCESSQUEUE);
		if(PID<0)
			OperatingSystem_ShowTime(ERROR);
		if(PID==NOFREEENTRY) ComputerSystem_DebugMessage(103,ERROR,userProgramsList[index]->executableName);
		else if(PID==PROGRAMDOESNOTEXIST) ComputerSystem_DebugMessage(104,ERROR,userProgramsList[index]->executableName,"it does not exist");
		else if(PID==PROGRAMNOTVALID) ComputerSystem_DebugMessage(104,ERROR,userProgramsList[index]->executableName,"invalid priority or size");
		else if(PID==TOOBIGPROCESS) ComputerSystem_DebugMessage(105,ERROR,userProgramsList[index]->executableName);
		else if(PID==MEMORYFULL) ComputerSystem_DebugMessage(144,ERROR,userProgramsList[index]->executableName);
		else{
			numberOfSuccessfullyCreatedProcesses++;
			OperatingSystem_ShowTime(INIT);
			ComputerSystem_DebugMessage(22,INIT,PID,userProgramsList[index]->executableName);
		}	
	}
	if(numberOfSuccessfullyCreatedProcesses>0) OperatingSystem_PrintStatus();
	numberOfNotTerminatedUserProcesses+=numberOfSuccessfullyCreatedProcesses;
	return numberOfSuccessfullyCreatedProcesses;
}


// This function creates a process from an executable program
int OperatingSystem_CreateProcess(USER_PROGRAMS_DATA executableProgram, int processType) {
  
	int PID;
	int processSize;
	int loadingPhysicalAddress;
	int priority;
	int loadProgram;
	FILE *programFile;
	int posBestPartition; 	// V4, exercise 6
	int partitionSize;		// V4, exercise 6

	// Obtain a process ID
	PID=OperatingSystem_ObtainAnEntryInTheProcessTable();
	if(PID==NOFREEENTRY) return NOFREEENTRY;
	
	// Obtain the memory requirements of the program
	processSize=OperatingSystem_ObtainProgramSize(&programFile, executableProgram.executableName);	
	if(processSize==PROGRAMDOESNOTEXIST) return PROGRAMDOESNOTEXIST;
	if(processSize==PROGRAMNOTVALID) return PROGRAMNOTVALID;
	
	// Obtain the priority for the process
	priority=OperatingSystem_ObtainPriority(programFile);
	if(priority==PROGRAMNOTVALID) return PROGRAMNOTVALID;
	
	// Obtain enough memory space, change in V4, exercise 6
 	posBestPartition=OperatingSystem_ObtainMainMemory(processSize, PID);
	if(posBestPartition==TOOBIGPROCESS) return TOOBIGPROCESS;
	if(posBestPartition==MEMORYFULL) return MEMORYFULL;		// added this
	loadingPhysicalAddress=partitionsTable[posBestPartition].initAddress;	// added this	
	
	// Load program in the allocated memory
	loadProgram=OperatingSystem_LoadProgram(programFile, loadingPhysicalAddress, processSize);
	if(loadProgram==TOOBIGPROCESS) return TOOBIGPROCESS;
	
	OperatingSystem_ShowPartitionTable("before allocating memory");
	partitionsTable[posBestPartition].occupied = 1;
	partitionsTable[posBestPartition].PID = PID;
	
	OperatingSystem_ShowTime(SYSMEM);
	partitionSize = partitionsTable[posBestPartition].size;
	ComputerSystem_DebugMessage(143,SYSMEM, posBestPartition, loadingPhysicalAddress, partitionSize, PID);
	OperatingSystem_ShowPartitionTable("after allocating memory");
	
	// PCB initialization
	OperatingSystem_PCBInitialization(PID, loadingPhysicalAddress, processSize, priority, processType);
	
	// Move process to the ready state
	OperatingSystem_MoveToTheREADYState(PID);
	
	return PID;
}


// Main memory is assigned in chunks. All chunks are the same size. A process
// always obtains the chunk whose position in memory is equal to the processor identifier
int OperatingSystem_ObtainMainMemory(int processSize, int PID) {
	int i, tooBig=0, posBestPartition=-1, sizeBestPartition=99999;
	
	OperatingSystem_ShowTime(SYSMEM);
	ComputerSystem_DebugMessage(142, SYSMEM, PID, processSize);
	
	for(i=0; i<PARTITIONTABLEMAXSIZE;i++){
		if(processSize<=partitionsTable[i].size)
			tooBig++;
	}
	// Make sure if there is any partition that can contain the process
	if(tooBig == 0) return TOOBIGPROCESS;
	
	for(i=0; i<PARTITIONTABLEMAXSIZE;i++){
		if(partitionsTable[i].occupied == 0){	// Partition not occupied
			if(partitionsTable[i].size >= processSize){		// Partition's size enough big to contain the process
				if(partitionsTable[i].size < sizeBestPartition){	// Partition's size smaller than the previous choice
					posBestPartition=i;
					sizeBestPartition=partitionsTable[i].size;
				}
			}
		}
	}
	if(posBestPartition<0) return MEMORYFULL;
	return posBestPartition;
	
	
 	//if (processSize>MAINMEMORYSECTIONSIZE)
		//return TOOBIGPROCESS;
	
 	//return PID*MAINMEMORYSECTIONSIZE;
}


// Assign initial values to all fields inside the PCB
void OperatingSystem_PCBInitialization(int PID, int initialPhysicalAddress, int processSize, int priority, int type) {

	processTable[PID].busy=1;
	processTable[PID].initialPhysicalAddress=initialPhysicalAddress;
	processTable[PID].processSize=processSize;
	OperatingSystem_ShowTime(SYSPROC);
	ComputerSystem_DebugMessage(111,SYSPROC,PID,statesNames[NEW]);
	processTable[PID].state=NEW;
	processTable[PID].priority=priority;
	processTable[PID].copyOfPCRegister=0;
	processTable[PID].copyOfPSWRegister=0;
	processTable[PID].copyOfAccumulator=0;
	processTable[PID].queueID = type;
}


// Move a process to the READY state: it will be inserted, depending on its priority, in
// a queue of identifiers of READY processes
void OperatingSystem_MoveToTheREADYState(int PID) {
	int queue = processTable[PID].queueID;
	if (Heap_add(PID, readyToRunQueue[queue],QUEUE_PRIORITY ,&numberOfReadyToRunProcesses[queue] ,PROCESSTABLEMAXSIZE)>=0) {
		// numberOfReadyToRunProcesses++;
		OperatingSystem_ShowTime(SYSPROC);
		ComputerSystem_DebugMessage(110,SYSPROC,PID,statesNames[processTable[PID].state],statesNames[READY]);
		processTable[PID].state=READY;
		//OperatingSystem_PrintReadyToRunQueue();
	}
}

// The STS is responsible of deciding which process to execute when specific events occur.
// It uses processes priorities to make the decission. Given that the READY queue is ordered
// depending on processes priority, the STS just selects the process in front of the READY queue
int OperatingSystem_ShortTermScheduler() {
	
	int selectedProcess;
	int queueType;
	queueType = USERPROCESSQUEUE;
	if (numberOfReadyToRunProcesses[USERPROCESSQUEUE] == 0) queueType=DAEMONSQUEUE;
	selectedProcess=OperatingSystem_ExtractFromReadyToRun(queueType);
	
	return selectedProcess;
}

// Return PID of more priority process in the READY queue
int OperatingSystem_ExtractFromReadyToRun(int queue) {
  
	int selectedProcess=NOPROCESS;
	selectedProcess=Heap_poll(readyToRunQueue[queue],QUEUE_PRIORITY ,&numberOfReadyToRunProcesses[queue]);
	
	// Return most priority process or NOPROCESS if empty queue
	return selectedProcess; 
}


// Function that assigns the processor to a process
void OperatingSystem_Dispatch(int PID) {

	// The process identified by PID becomes the current executing process
	executingProcessID=PID;
	// Change the process' state
	OperatingSystem_ShowTime(SYSPROC);
	ComputerSystem_DebugMessage(110,SYSPROC,PID,statesNames[processTable[PID].state],statesNames[EXECUTING]);
	processTable[PID].state=EXECUTING;
	// Modify hardware registers with appropriate values for the process identified by PID
	OperatingSystem_RestoreContext(PID);
}


// Modify hardware registers with appropriate values for the process identified by PID
void OperatingSystem_RestoreContext(int PID) {
  
	// New values for the CPU registers are obtained from the PCB
	Processor_CopyInSystemStack(MAINMEMORYSIZE-1,processTable[PID].copyOfPCRegister);
	Processor_CopyInSystemStack(MAINMEMORYSIZE-2,processTable[PID].copyOfPSWRegister);
	Processor_CopyInSystemStack(MAINMEMORYSIZE-3,processTable[PID].copyOfAccumulator);
	
	// Same thing for the MMU registers
	MMU_SetBase(processTable[PID].initialPhysicalAddress);
	MMU_SetLimit(processTable[PID].processSize);
}


// Function invoked when the executing process leaves the CPU 
void OperatingSystem_PreemptRunningProcess() {

	// Save in the process' PCB essential values stored in hardware registers and the system stack
	OperatingSystem_SaveContext(executingProcessID);
	// Change the process' state
	OperatingSystem_MoveToTheREADYState(executingProcessID);
	// The processor is not assigned until the OS selects another process
	executingProcessID=NOPROCESS;
}


// Save in the process' PCB essential values stored in hardware registers and the system stack
void OperatingSystem_SaveContext(int PID) {
	
	// Load PC saved for interrupt manager
	processTable[PID].copyOfPCRegister=Processor_CopyFromSystemStack(MAINMEMORYSIZE-1);
	
	// Load PSW saved for interrupt manager
	processTable[PID].copyOfPSWRegister=Processor_CopyFromSystemStack(MAINMEMORYSIZE-2);
	
	// Load Accumulator for interrupt manager
	processTable[PID].copyOfAccumulator=Processor_CopyFromSystemStack(MAINMEMORYSIZE-3);
	
}


// Exception management routine
void OperatingSystem_HandleException() {
  
	OperatingSystem_ShowTime(INTERRUPT);
	// Show message "Process [executingProcessID] has generated an exception and is terminating\n"
	// ComputerSystem_DebugMessage(23,SYSPROC,executingProcessID); commented after exercise 2 V4
	int exceptionID=Processor_GetRegisterB();
	switch (exceptionID) {
		case DIVISIONBYZERO:
			ComputerSystem_DebugMessage(140, INTERRUPT, executingProcessID, "division by zero");
			break;
		case INVALIDPROCESSORMODE:
			ComputerSystem_DebugMessage(140, INTERRUPT, executingProcessID, "invalid processor mode");
			break;
		case INVALIDADDRESS:
			ComputerSystem_DebugMessage(140, INTERRUPT, executingProcessID, "invalid address");
			break;
		case INVALIDINSTRUCTION:
			ComputerSystem_DebugMessage(140, INTERRUPT, executingProcessID, "invalid instruction");
			break;
	}
	OperatingSystem_TerminateProcess();
	OperatingSystem_PrintStatus();
}


// All tasks regarding the removal of the process
void OperatingSystem_TerminateProcess() {
  
	int selectedProcess;
	OperatingSystem_ShowTime(SYSPROC);
  	ComputerSystem_DebugMessage(110,SYSPROC,executingProcessID,statesNames[processTable[executingProcessID].state],statesNames[EXIT]);
	processTable[executingProcessID].state=EXIT;
	processTable[executingProcessID].busy=0;
	OperatingSystem_ReleaseMainMemory(executingProcessID);
	
	// One more process that has terminated
	numberOfNotTerminatedUserProcesses--;
	
	if (numberOfNotTerminatedUserProcesses<=0  && OperatingSystem_IsThereANewProgram() == -1) {
		// Simulation must finish 
		OperatingSystem_ReadyToShutdown();
	}
	// Select the next process to execute (sipID if no more user processes)
	selectedProcess=OperatingSystem_ShortTermScheduler();
	// Assign the processor to that process
	OperatingSystem_Dispatch(selectedProcess);
}


// System call management routine
void OperatingSystem_HandleSystemCall() {
  
	int systemCallID, selectedProcess;

	// Register A contains the identifier of the issued system call
	systemCallID=Processor_GetRegisterA();
	
	switch (systemCallID) {
		case SYSCALL_PRINTEXECPID:
			// Show message: "Process [executingProcessID] has the processor assigned\n"
			OperatingSystem_ShowTime(SYSPROC);
			ComputerSystem_DebugMessage(24,SYSPROC,executingProcessID);
			break;

		case SYSCALL_END:
			// Show message: "Process [executingProcessID] has requested to terminate\n"
			OperatingSystem_ShowTime(SYSPROC);
			ComputerSystem_DebugMessage(25,SYSPROC,executingProcessID);
			OperatingSystem_TerminateProcess();
			OperatingSystem_PrintStatus();
			break;
			
		case SYSCALL_YIELD:
			OperatingSystem_HandleYield();
			break;
		
		case SYSCALL_SLEEP:
			OperatingSystem_SaveContext(executingProcessID);
			OperatingSystem_MoveToTheBLOCKEDState(executingProcessID);
			selectedProcess = OperatingSystem_ShortTermScheduler();
			OperatingSystem_Dispatch(selectedProcess);
			OperatingSystem_PrintStatus();
			break;
		case SYSCALL_MEMFIT:
			OperatingSystem_SearchForBestPartition(executingProcessID);
		default:
			OperatingSystem_ShowTime(INTERRUPT);
			ComputerSystem_DebugMessage(141,INTERRUPT,executingProcessID,systemCallID);
			OperatingSystem_TerminateProcess();
			OperatingSystem_PrintStatus();
			break;
	}
}
	
//	Implement interrupt logic calling appropriate interrupt handle
void OperatingSystem_InterruptLogic(int entryPoint){
	switch (entryPoint){
		case SYSCALL_BIT: // SYSCALL_BIT=2
			OperatingSystem_HandleSystemCall();
			break;
		case EXCEPTION_BIT: // EXCEPTION_BIT=6
			OperatingSystem_HandleException();
			break;
		case CLOCKINT_BIT: // EXCEPTION_BIT=9
			OperatingSystem_HandleClockInterrupt();
			break;
	}

}

// ********** MY METHODS ********** 

void OperatingSystem_PrintReadyToRunQueue(){
	int i;
	int localPID;
	int c;
	int queueType;
	OperatingSystem_ShowTime(SHORTTERMSCHEDULE);
	ComputerSystem_DebugMessage(106,SHORTTERMSCHEDULE);
	for (queueType = 0; queueType < NUMBEROFQUEUES; queueType++){
		if (queueType==0) {
			ComputerSystem_DebugMessage(112,SHORTTERMSCHEDULE);
			c=0;
		}
		else{
			ComputerSystem_DebugMessage(113,SHORTTERMSCHEDULE);
			c=0;
		}
		for (i=0 ;i<numberOfReadyToRunProcesses[queueType] ; i++) {
			localPID = readyToRunQueue[queueType][i];
			if(c==0){
				//ComputerSystem_DebugMessage(108,SHORTTERMSCHEDULE," ");
				ComputerSystem_DebugMessage(107,SHORTTERMSCHEDULE,localPID,processTable[localPID].priority);
				c++;
			}
			else{
				ComputerSystem_DebugMessage(108,SHORTTERMSCHEDULE,", ");
				ComputerSystem_DebugMessage(107,SHORTTERMSCHEDULE,localPID,processTable[localPID].priority);
			}
		}
		ComputerSystem_DebugMessage(108,SHORTTERMSCHEDULE,"\n");
	}
	
}


void OperatingSystem_HandleYield(){
	int ProcessInExecution, ProcessToChange, queueTypeLocal;
	ProcessInExecution= executingProcessID;
	queueTypeLocal = processTable[ProcessInExecution].queueID;
	if (numberOfReadyToRunProcesses[queueTypeLocal]>0 && processTable[ProcessInExecution].priority == processTable[readyToRunQueue[queueTypeLocal][0]].priority){
		ProcessToChange = readyToRunQueue[queueTypeLocal][0];
		OperatingSystem_ShowTime(SHORTTERMSCHEDULE);
		ComputerSystem_DebugMessage(115,SHORTTERMSCHEDULE,ProcessInExecution, ProcessToChange);
		OperatingSystem_PreemptRunningProcess();
		OperatingSystem_ExtractFromReadyToRun(queueTypeLocal);
		OperatingSystem_RestoreContext(ProcessToChange);
		OperatingSystem_Dispatch(ProcessToChange);
		OperatingSystem_PrintStatus();
	}
}

// Exercise 2-b of V2
// Exercise 6-a of V2
void OperatingSystem_HandleClockInterrupt(){
	int i=0, localPID, unloocked=0, ProcessInExecution, ProcessToChange, queueTypeLocal;
	OperatingSystem_ShowTime(INTERRUPT);
	numberOfClockInterrupts++;
	ComputerSystem_DebugMessage(120,INTERRUPT,numberOfClockInterrupts);
	while(processTable[sleepingProcessesQueue[i]].whenToWakeUp==numberOfClockInterrupts){
		localPID=OperatingSystem_ExtractFromBLOCKED();
		OperatingSystem_MoveToTheREADYState(localPID);
		unloocked=1;
		i++;
	}
	if(OperatingSystem_LongTermScheduler()>0) unloocked=1;
	if(numberOfNotTerminatedUserProcesses<=0 && numberOfProgramsInArrivalTimeQueue<=0) OperatingSystem_ReadyToShutdown();
	if(unloocked){
		if(OperatingSystem_ExecutingProcessIsMostPriorityProcess()){
			ProcessInExecution=executingProcessID;
			queueTypeLocal = USERPROCESSQUEUE;
			if (numberOfReadyToRunProcesses[USERPROCESSQUEUE] == 0) queueTypeLocal=DAEMONSQUEUE;
			ProcessToChange = OperatingSystem_ExtractFromReadyToRun(queueTypeLocal);
			OperatingSystem_ShowTime(SHORTTERMSCHEDULE);
			ComputerSystem_DebugMessage(121,SHORTTERMSCHEDULE,ProcessInExecution,ProcessToChange);
			OperatingSystem_PreemptRunningProcess();
			OperatingSystem_Dispatch(ProcessToChange);
			OperatingSystem_PrintStatus();
		}
	}
}

// Move a process to the BLOCKED state: it will be inserted, depending on acumulator plus the number of clock interruptions plus 1, in
// a queue of identifiers of BLOCKED processes
void OperatingSystem_MoveToTheBLOCKEDState(int PID) {
	processTable[PID].whenToWakeUp=abs(Processor_GetAccumulator())+numberOfClockInterrupts+1;
	if (Heap_add(PID, sleepingProcessesQueue,QUEUE_WAKEUP ,&numberOfSleepingProcesses ,PROCESSTABLEMAXSIZE)>=0) {
		OperatingSystem_ShowTime(SYSPROC);
		ComputerSystem_DebugMessage(110,SYSPROC,PID,statesNames[processTable[executingProcessID].state],statesNames[BLOCKED]);
		processTable[PID].state=BLOCKED;
		//OperatingSystem_PrintReadyToRunQueue();
	}
}

// Return PID of more whenToWakeUp process in the BLOCKED queue
int OperatingSystem_ExtractFromBLOCKED() {
  
	int selectedProcess=NOPROCESS;
	selectedProcess=Heap_poll(sleepingProcessesQueue,QUEUE_WAKEUP ,&numberOfSleepingProcesses);
	return selectedProcess; 
}

int OperatingSystem_ExecutingProcessIsMostPriorityProcess(){
	int queueType = USERPROCESSQUEUE;
	if (numberOfReadyToRunProcesses[USERPROCESSQUEUE] == 0) queueType=DAEMONSQUEUE;
	int localPriority = processTable[readyToRunQueue[queueType][0]].priority;
	if(queueType==USERPROCESSQUEUE || localPriority < processTable[executingProcessID].priority) return 1;
	else return 0;
	// if(queueType == USERPROCESSQUEUE){
		// if(localPriority < processTable[executingProcessID].priority) return 1;
		// else return 0;
	// }
	// else{
		// if(numberOfReadyToRunProcesses[USERPROCESSQUEUE] || (numberOfReadyToRunProcesses[DAEMONSQUEUE] && localPriority < processTable[executingProcessID].priority)) return 1;
		// else return 0;
	// }
}

int OperatingSystem_GetExecutingProcessID() {
	return executingProcessID;
}

// Release the partition that was used 
void OperatingSystem_ReleaseMainMemory(int PID){
	int i, localInitAddress, localSize;
	for(i=0; i<PARTITIONTABLEMAXSIZE; i++){
		if(PID == partitionsTable[i].PID && partitionsTable[i].occupied==1){
			OperatingSystem_ShowPartitionTable("before releasing memory");
			partitionsTable[i].occupied=0;
			partitionsTable[i].PID = -1;
			localInitAddress = partitionsTable[i].initAddress;
			localSize = partitionsTable[i].size;
			OperatingSystem_ShowTime(SYSMEM);
			ComputerSystem_DebugMessage(145,SYSMEM, i, localInitAddress, localSize, PID);
			OperatingSystem_ShowPartitionTable("after releasing memory");
			break;
		}
	}
}

void OperatingSystem_SearchForBestPartition(int PID){
	int actualPartition, posBestPartition=-1, sizeBestPartition, processSize, i;
	for(i=0; i<PARTITIONTABLEMAXSIZE; i++){
		if(PID == partitionsTable[i].PID && partitionsTable[i].occupied==1){
			actualPartition = i;
			sizeBestPartition = partitionsTable[i].size;
			processSize = processTable[PID].processSize;
			break;
		}
	}
	OperatingSystem_ShowTime(SYSMEM);
	ComputerSystem_DebugMessage(44,SYSMEM, PID, actualPartition);
	for(i=0; i<PARTITIONTABLEMAXSIZE;i++){
		if(partitionsTable[i].occupied == 0){
			if(partitionsTable[i].size >= processSize){
				if(partitionsTable[i].size < sizeBestPartition){
					posBestPartition=i;
					sizeBestPartition=partitionsTable[i].size;
				}
			}
		}
	}
	if(posBestPartition!= -1){
		int from, to, size;
		OperatingSystem_ShowPartitionTable("before moving process");
		OperatingSystem_ShowTime(SYSMEM);
		ComputerSystem_DebugMessage(45,SYSMEM, PID, actualPartition, posBestPartition);
		
		OperatingSystem_SaveContext(PID);
		processTable[PID].initialPhysicalAddress = partitionsTable[posBestPartition].initAddress;
		
		from = partitionsTable[actualPartition].initAddress;
		to = partitionsTable[posBestPartition].initAddress;
		size = processTable[PID].processSize;
		OperatingSystem_MoveMemoryFromTo(from, to, size);
		
		OperatingSystem_RestoreContext(PID);
		OperatingSystem_ShowPartitionTable("after moving process");
	}
	
	
}