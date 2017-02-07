#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <hardware.h>
#include <terminals.h>
#include <threads.h>

#define MAX_ECHO_BUFFER_LENGTH 1024
#define MAX_TERMINAL_BUFFER_LENGTH 1024
#define MAX_WRITE_BUFFER_LENGTH 1024

/*
 * The state of each of the terminals
 * 
 * This is declared 'static' so it can't be seen outside this .c file.
 */
static int state[MAX_NUM_TERMINALS];
#define	SITTING		0
#define	READING		1
#define	WRITING		2
#define ECHOING     3
static int readingState[MAX_NUM_TERMINALS];
#define NOT_READING 0

static cond_id_t cond_var[MAX_NUM_TERMINALS];

static int echoBufferLength[MAX_NUM_TERMINALS];
static int echoItemsLeft[MAX_NUM_TERMINALS];
static char echoBuffer[MAX_NUM_TERMINALS][MAX_ECHO_BUFFER_LENGTH];
static int echoBufferSpot[MAX_NUM_TERMINALS];

static int writeItemsLeft[MAX_NUM_TERMINALS];
static char writeBuffer[MAX_NUM_TERMINALS][MAX_WRITE_BUFFER_LENGTH];
static int writeBufferLength[MAX_NUM_TERMINALS];
static int writeBufferSpot[MAX_NUM_TERMINALS];

static int terminalItemsLeft[MAX_NUM_TERMINALS];
static int terminalItemsWrittenSoFar[MAX_NUM_TERMINALS];
static char terminalBuffer[MAX_NUM_TERMINALS][MAX_ECHO_BUFFER_LENGTH];
static int terminalBufferLength[MAX_NUM_TERMINALS];
static int terminalBufferSpot[MAX_NUM_TERMINALS];

/*
 * Output buflen chars from buf to screen, blocking until all characters
 * are displayed.  Returns the number of characters written or -1 for 
 * error. Writes to terminal term.
 */
extern int WriteTerminal(int term, char *buf, int buflen) {
	Declare_Monitor_Entry_Procedure();
	//	printf("Writing terminal %d\n", term);
	if (buflen + writeBufferLength[term] > MAX_WRITE_BUFFER_LENGTH) {
		int amntTillEnd = MAX_WRITE_BUFFER_LENGTH - (writeBufferLength[term] % MAX_WRITE_BUFFER_LENGTH);
		if (amntTillEnd == MAX_WRITE_BUFFER_LENGTH) {
			amntTillEnd = 0;
		}
		memcpy(writeBuffer[term]+(writeBufferLength[term] % MAX_WRITE_BUFFER_LENGTH), buf, amntTillEnd);
		int amntForBeg = buflen - amntTillEnd;
		memcpy(writeBuffer[term], buf + amntTillEnd, amntForBeg);		
	}
	else {
		memcpy(writeBuffer[term]+(writeBufferLength[term] % MAX_WRITE_BUFFER_LENGTH), buf, buflen);
	}
	writeItemsLeft[term] += buflen;
	writeBufferLength[term] += buflen;
	if (state[term] == SITTING) {
		char reg_char = writeBuffer[term][writeBufferSpot[term] % MAX_WRITE_BUFFER_LENGTH];
		state[term] = WRITING;
		writeItemsLeft[term]--;
		writeBufferSpot[term]++;
		WriteDataRegister(term, reg_char);
	}
	//	if (writeItemsLeft[term] == 0) {
		return buflen;
		//}
		//else {
       	 	//CondWait(cond_var[term]);
		//	}
}

/*
 * Read characters from terminal and place into buf until buflen chars
 * have been read or a '\n' is read.  Returns the number of characters
 * read or -1 for error.  Like the Unix read() call but unlike normal
 * C programming language strings, no null character is automatically
 * added onto the end of the buffer by this call.  Reads from
 * terminal term.
 */
extern int ReadTerminal(int term, char *buf, int buflen) {
	Declare_Monitor_Entry_Procedure();
	printf("Reading terminal %d\n", term);
	if (buflen >= 0) {
	  while(readingState[term] == READING) {
	    CondWait(cond_var[term]);
	  }
	        readingState[term] = READING;
		terminalItemsWrittenSoFar[term] = 0;
		while (1) {
			char reg_char = 0;
			//		printf("Looping\n");
			if (terminalBufferSpot[term] < terminalBufferLength[term]) {
			  reg_char = terminalBuffer[term][terminalBufferSpot[term] % MAX_TERMINAL_BUFFER_LENGTH];
			  // printf("reg_char %c\n", reg_char);
				terminalBufferSpot[term]++;
				if (reg_char == '\b') {
					if (terminalItemsWrittenSoFar[term] > 0) {
						terminalItemsWrittenSoFar[term]--;					
					}
				}
				else {
					buf[terminalItemsWrittenSoFar[term]] = reg_char;
					//printf("write %c\n", buf[terminalItemsWrittenSoFar[term]]);
					terminalItemsWrittenSoFar[term]++;
					
					//		printf("num %d\n", terminalItemsWrittenSoFar[term]);
				}
			}
			else {
				CondWait(cond_var[term]);
			}
			if (terminalItemsWrittenSoFar[term] == buflen || reg_char == '\n' || reg_char == '\r') {
			  //printf("This is running. terminalItems %d buflen %d reg_char %d\nstr:%s\n", terminalItemsWrittenSoFar[term], buflen, reg_char, buf);
			  readingState[term] = NOT_READING;
			  return terminalItemsWrittenSoFar[term];
			}
		}
	}
	else {
		return -1;
	}
}

/*
 * Must be called once by application before any invocation of 
 * WriteTerminal or ReadTerminal on terminal number term.  Must call
 * the hardware's InitHardware for term.  Returns 0 if OK, or -1
 * for any error.
 */
int InitTerminal(int term) {	
	echoBufferLength[term] = 0;
	writeBufferLength[term] = 0;
	terminalBufferLength[term] = 0;

	terminalItemsLeft[term] = 0;
	echoItemsLeft[term] = 0;
	writeItemsLeft[term] = 0;
	
	echoBufferSpot[term] = 0;
	writeBufferSpot[term] = 0;
	terminalBufferSpot[term] = 0;
    
    terminalItemsWrittenSoFar[term] = 0;
    InitHardware(term);

    cond_var[term] = CondCreate();
    readingState[term] = NOT_READING;
	return 0;
}

/*
 * Must be called exactly once before any other terminal API calls.
 * Initializes the terminal driver itself, not any particular
 * terminal.  Returns 0 if OK, or -1 for any error.
 */
int InitTerminalDriver(void) {
	return 0;
}

/*
 * ----------------------------------------------------------------------
 * IMPORTANT:
 * You *must* write routines as part of your terminal driver to handle
 * these two interrupts.  The procedure names must be "TransmitInterrupt"
 * and "ReceiveInterrupt" as shown below:
 * ----------------------------------------------------------------------
 */

/*
 * TransmitInterrupt is called once for each character written to the
 * data register after the character has been written to the screen
 * of the terminal.
 */
void TransmitInterrupt(int term) {
	Declare_Monitor_Entry_Procedure();
	//printf("TransmitInturrupt\n");
	char reg_char = 0;

	if (echoItemsLeft[term] != 0) {
		echoItemsLeft[term]--;
		reg_char = echoBuffer[term][echoBufferSpot[term] % MAX_ECHO_BUFFER_LENGTH];
		echoBufferSpot[term]++;
	}
	else {
		if (writeItemsLeft[term] != 0) {
			writeItemsLeft[term]--;
			reg_char = writeBuffer[term][writeBufferSpot[term] % MAX_WRITE_BUFFER_LENGTH];
			writeBufferSpot[term]++;		
		}
	}

	if (reg_char != 0) {
		state[term] = WRITING;
		WriteDataRegister(term, reg_char);
	}
	else {
		state[term] = SITTING;
		CondSignal(cond_var[term]);
	}
		

}

/*
 * ReceiveInterrupt is called once for every character typed on the
 * keyboard of the terminal after the character has been placed in the
 * data register 
 */
void ReceiveInterrupt(int term) {
	Declare_Monitor_Entry_Procedure();
	//printf("RecieveInturrupt %d\n", term);
	char reg_char = ReadDataRegister(term);
	echoBuffer[term][echoBufferLength[term] % MAX_ECHO_BUFFER_LENGTH] = reg_char;
	terminalBuffer[term][terminalBufferLength[term] % MAX_TERMINAL_BUFFER_LENGTH] = reg_char;

	echoItemsLeft[term]++;

	echoBufferLength[term]++;
	terminalBufferLength[term]++;
	
	if (reg_char == '\r') {
		echoBuffer[term][echoBufferLength[term] % MAX_ECHO_BUFFER_LENGTH] = '\n';
		echoItemsLeft[term]++;
		echoBufferLength[term]++;
	}
	if (reg_char == '\b' || reg_char == '\177') {
		echoBuffer[term][echoBufferLength[term] % MAX_ECHO_BUFFER_LENGTH] = ' ';
		echoItemsLeft[term]++;
		echoBufferLength[term]++;
		echoBuffer[term][echoBufferLength[term] % MAX_ECHO_BUFFER_LENGTH] = '\b';
		echoItemsLeft[term]++;
		echoBufferLength[term]++;
	}

	if (state[term] == SITTING) {
		state[term] = ECHOING;
		echoItemsLeft[term]--;
		echoBufferSpot[term]++;

		WriteDataRegister(term, reg_char);
	}
}
