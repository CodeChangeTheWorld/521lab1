#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <terminals.h>
#include <threads.h>
#include <hardware.h>

#define MAX_BUFF_LEN 1024

/* Echo buffer */
char echo_buff[NUM_TERMINALS][MAX_BUFF_LEN];
/* Echo Buffer address */
int echo_buff_addr[NUM_TERMINALS];
/* Record first WriteDataRegister char */
int echo_first_addr[NUM_TERMINALS];
/* Record last WriteDataRegister char */
int echo_last_addr[NUM_TERMINALS];

/* Input buffer */
char input_buff[NUM_TERMINALS][MAX_BUFF_LEN];
/* Input buffer address */
int input_buff_addr[NUM_TERMINALS];
/* Read from input buffer start point */
int input_read_start[NUM_TERMINALS];

/* Output buffer, pointer to the next character address */
char *output_buff_addr[NUM_TERMINALS];
/* Output first char addr buffer */
int output_first_addr[NUM_TERMINALS];
/* Output string length */
int output_len[NUM_TERMINALS];
/* Justify if '\r' have been translated in writeterminal*/
bool output_r_trans[NUM_TERMINALS];

/* How many attempts to write on the same terminal */
int writing_num[NUM_TERMINALS];

/* Condition variables to lock */
static cond_id_t write[NUM_TERMINALS];

/* Array for TerminalDriverStatistics */
struct termstat stat[NUM_TERMINALS];

/* State array*/
int state[NUM_TERMINALS];
#define WAITING 0
#define READING 1
#define WRITING 2
#define ECHOING 3

/* Interrupt handler1
 * When the receipt of a new character from a keyboard completes,
 * the terminal hardware signals a receive interrupt.
 */
void ReceiveInterrupt(int term) {
	Declare_Monitor_Entry_Procedure();

	char character = ReadDataRegister(term);
	int input_addr = input_buff_addr[term];
	int echo_addr = echo_buff_addr[term];
	echo_first_addr[term] = echo_addr;

	/* put this character into the input buffer */
	if (character == '\b') {
		if (input_buff_addr[term] != 0) { 
			input_buff_addr[term] --;
		}
		echo_buff[term][echo_addr] = '\b';
		echo_buff[term][echo_addr+1] = ' ';
		echo_buff[term][echo_addr+2] = '\b';
		echo_buff_addr[term] +=3;
		echo_last_addr[term] = echo_buff_addr[term] - 1;
	} else {
		if (character == '\r') {
			input_buff[term][input_addr] = '\n';
			input_buff_addr[term] ++;
			echo_buff[term][echo_addr] = '\r';
			echo_buff[term][echo_addr+1] = '\n';
			echo_buff_addr[term] += 2;	
			echo_last_addr[term] = echo_buff_addr[term] - 1;	
		} else {
			input_buff[term][input_addr] = character;
			input_buff_addr[term] ++;
			echo_buff[term][echo_addr] = character;
			echo_buff_addr[term] ++;
			echo_last_addr[term] = echo_buff_addr[term]  ;		
		}
	}	
	if (state[term] != WRITING) {
		stat[term].tty_out ++;
		WriteDataRegister(term, character);
	}	
}
/* Interrupt handler2
 * when the transmission of a character to a terminal completes, 
 * the terminal hardware signals a transmit interrupt.
 */
void TransmitInterrupt(int term) {
	Declare_Monitor_Entry_Procedure();
	if( echo_last_addr[term] != echo_first_addr[term]) {
		stat[term].tty_out ++;
		WriteDataRegister(term, echo_buff[term][echo_first_addr[term]]);
		echo_first_addr[term] ++;
	}  else {
		if (output_buff_addr[term][output_first_addr[term]] == '\n' && output_r_trans[term]) {
			output_r_trans[term] = false;
			state[term] = WRITING;
			stat[term].tty_out ++;
			WriteDataRegister(term, '\n');
			output_first_addr[term] ++;
		} else if (output_buff_addr[term][output_first_addr[term]] == '\n' && !output_r_trans[term]){
			output_r_trans[term] = true;
			WriteDataRegister(term, '\r');
		} else if ( output_first_addr[term] != output_len[term]) {
			state[term] = WRITING;
			stat[term].tty_out ++;
			output_first_addr[term] ++;
			WriteDataRegister(term, output_buff_addr[term][output_first_addr[term]]);
			
		} else {
			printf("%c\n", 'c');
			writing_num[term] --;
			output_first_addr[term] = 0;
			CondSignal(write[term]);
		}
	} 
}
/* 
 * Read characters from terminal and place into buf until buflen chars
 * have been read or a '\n' is read. 
 */
int ReadTerminal(int term, char *buf, int buflen){
	// Declare_Monitor_Entry_Procedure();
	// stat[term].user_out += buflen;
	// if (buflen < 0) return -1;
	// if (buflen == 0) return 0;
	// int i;
	// char *current_buf=buf;
	// for (i = input_read_start[term]; i < buflen; i ++) {
	// 	char c = input_buff[term][i];
	// 	if (c == '\n') {
	// 		input_read_start[term] = i + 1;
	// 		break;
	// 	} else {
	// 		*current_buf = c;
	// 		current_buf ++;
	// 	}
	// }



	return buflen;
}

/* 
 * Copy the character from term into buf
 * Called by some user process (thread) whenever it wants to output something. Roughly equivalent to printf.
 * term: terminal number
 */
int WriteTerminal(int term, char *buf, int buflen) {
	Declare_Monitor_Entry_Procedure();
	stat[term].user_in += buflen;
	if (buflen < 0) return 0;
	/* If there are some other threads writing */
	while (writing_num[term] >0  ) {
		CondWait(write[term]);
	} 
	/* start a new writing process */
	writing_num[term] ++;
	printf("%s\n", buf);
	output_buff_addr[term] = buf;
	//output_first_addr[term] ++;
	output_len[term] = buflen;
	state[term] = WRITING;
	stat[term].tty_out ++;
	if (*buf == '\n') {
		output_r_trans[term] = true;
		WriteDataRegister(term, '\r');
	} else {
		WriteDataRegister(term, output_buff_addr[term][0]);
	}
	
	
	return buflen;
}

int InitTerminal(int term) {
	if (InitHardware(term) == 0) {
		echo_buff_addr[term] = 0;
		input_buff_addr[term] = 0;
		output_buff_addr[term] = 0;
		input_read_start[term] = 0;
		state[term] = WAITING;
		write[term] = CondCreate();
		stat[term].tty_in = 0;
		stat[term].tty_out = 0;
		stat[term].user_in = 0;
		stat[term].user_out = 0;

		writing_num[term] = 0;
		output_r_trans[term] = false;
		return 0;
	} else {
		return -1;
	}
	
}

int InitTerminalDriver() {
	Declare_Monitor_Entry_Procedure();
	int i;
	for (i = 0; i < NUM_TERMINALS; i++) {
		stat[i].tty_in = -1;
		stat[i].tty_out = -1;
		stat[i].user_in = -1;
		stat[i].user_out = -1;
	}
	return 0;
}

int TerminalDriverStatistics(struct termstat *stats) {
	Declare_Monitor_Entry_Procedure();
	int i;
	for (i = 0; i < NUM_TERMINALS; i++) {
		stats[i].tty_in = stat[i].tty_in;
		stats[i].tty_out = stat[i].tty_out;
		stats[i].user_in = stat[i].user_in;
		stats[i].user_out = stat[i].user_out;
	}
	return 0;
}


