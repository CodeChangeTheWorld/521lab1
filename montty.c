#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <threads.h>
#include <hardware.h>
#include <terminals.h>

/* Constants */
#define ECHO_BUF_SIZE 1024 // Must be at least 2 since one is reserved for \7
#define INPUT_BUF_SIZE 4096

/* Condition variables to lock */
static cond_id_t writer[MAX_NUM_TERMINALS];
static cond_id_t writing[MAX_NUM_TERMINALS];
static cond_id_t busy_echo[MAX_NUM_TERMINALS];
static cond_id_t reader[MAX_NUM_TERMINALS];
static cond_id_t toRead[MAX_NUM_TERMINALS];

/* Buffer for the echo characters */
char echo_buf[MAX_NUM_TERMINALS][ECHO_BUF_SIZE]; // Must be at least 3

/* Buffer for the input characters */
char input_buf[MAX_NUM_TERMINALS][INPUT_BUF_SIZE];

/* To keep track of open terminals */
int open_terminal[MAX_NUM_TERMINALS];

/* Counter for the number of characters in echo_buf that needs to be echoed */
int echo_count[MAX_NUM_TERMINALS];

/* Counter for actual number of character on screen */
int screen_len[MAX_NUM_TERMINALS];

/* Counter for echo buffer writing and reading index */
int echo_buf_write_index[MAX_NUM_TERMINALS];
int echo_buf_read_index[MAX_NUM_TERMINALS];

/* Boolean to check echo should initiate or not */
bool initiate_echo[MAX_NUM_TERMINALS];

/* Boolean to check for back spacing */
bool first_backspace[MAX_NUM_TERMINALS];

/* Keep track of the number of writers */
int num_writers[MAX_NUM_TERMINALS];

/* Keep track of the number of waiting writers */
int num_waiting[MAX_NUM_TERMINALS];

/* Counter for the number of characters in WriteTerminal buf */
int writeT_buf_count[MAX_NUM_TERMINALS];

/* The length of the WriteTerminal buffer */
int writeT_buf_length[MAX_NUM_TERMINALS];

/* Keep track of whether a newline was written or not */
bool writeT_first_newline[MAX_NUM_TERMINALS];

/* Pointer for the buffer in WriteTerminal */
char *writeT_buf[MAX_NUM_TERMINALS];

/* Keep track of the number of readers */
int num_readers[MAX_NUM_TERMINALS];

/* Counter for the number of input that are readable */
int num_readable_input[MAX_NUM_TERMINALS];

/* Counter for input buffer writing and reading index */
int input_buf_write_index[MAX_NUM_TERMINALS];
int input_buf_read_index[MAX_NUM_TERMINALS];

/* Counter for the number of characters in input_buf */
int input_buf_count[MAX_NUM_TERMINALS];

/* Array for TerminalDriverStatistics */
struct termstat statistics[MAX_NUM_TERMINALS];

/*
 * Writes given buffer to the Terminal
 */
extern
int WriteTerminal(int term, char *buf, int buflen)
{
	Declare_Monitor_Entry_Procedure();

	/* Error Handling */
	if ((buflen < 1) || (open_terminal[term] < 0) ||
		(term < 0) || (term > MAX_NUM_TERMINALS - 1))
		return -1;

	/* 
	 * Check if you can enter. That is, if there isn't anyone 
	 * else writing on the same terminal.
	 */
	if ((num_writers[term] > 0) || num_waiting[term] > 0) {
		num_waiting[term]++;
		CondWait(writer[term]);
	} else {
		num_waiting[term]++;
	}
	num_writers[term]++;
	num_waiting[term]--;

	/* Wait until echo is done */
	if (!initiate_echo[term])
		CondWait(busy_echo[term]);

	/* Output to the screen */
	writeT_buf[term] = buf;
	writeT_buf_count[term] = buflen;
	writeT_buf_length[term] = buflen;

	if (writeT_buf[term][0] == '\n') {
		WriteDataRegister(term, '\r');
		writeT_buf_count[term]++;
		statistics[term].user_in--;
		writeT_first_newline[term] = false;
	} else {
		WriteDataRegister(term, writeT_buf[term][0]);
		writeT_first_newline[term] = true;
	}

	/* Wait until writing is done */
	CondWait(writing[term]);
	num_writers[term]--;
	CondSignal(writer[term]);
	return buflen;
}

/*
 * Stores the input buffer of given length or upto \n, which every is shorter, to the pointer passed in
 */
extern
int ReadTerminal(int term, char *buf, int buflen)
{
	Declare_Monitor_Entry_Procedure();
	char c;
	int i;

	/* Error Handling */
	if ((buflen < 1) || (buflen > INPUT_BUF_SIZE) || (open_terminal[term] < 0) || 
		(term < 0) || (term > MAX_NUM_TERMINALS - 1))
		return -1;

	/* 
	 * Check if you can enter. That is, if there isn't anyone 
	 * else reading on the same terminal.
	 */
	if (num_readers[term] > 0) {
		CondWait(reader[term]);
	}
	num_readers[term]++;

	/* Wait until we can read */
	if (num_readable_input[term] == 0)
		CondWait(toRead[term]);
	num_readable_input[term]--;

	/* Read from input */
	for (i = 0; i < buflen;) {
		c = input_buf[term][input_buf_read_index[term]];
		input_buf_read_index[term] = (input_buf_read_index[term] + 1) % INPUT_BUF_SIZE;
		strncat(buf, &c, 1);
		/* Statistics since the boundary has been crossed */
		statistics[term].user_out++;
		i++;
		input_buf_count[term]--;
		if ('\n' == c)
			break;
	}

	if ('\n' != c)
		num_readable_input[term]++;
	
	num_readers[term]--;
	CondSignal(reader[term]);

	return i;
}

/*
 * Initialize all the variables for the terminal
 */
extern
int InitTerminal(int term)
{
	Declare_Monitor_Entry_Procedure();

	/* Error Handling */
	if ((term < 0) || (term > MAX_NUM_TERMINALS - 1) || (open_terminal[term] == 0))
		return -1;

	/* Try initializing hardware */
	open_terminal[term] = InitHardware(term);

	if (open_terminal[term] == 0) {
		// Condition variables
		writer[term] = CondCreate();
		writing[term] = CondCreate();
		busy_echo[term] = CondCreate();
		reader[term] = CondCreate();
		toRead[term] = CondCreate();

		// WriteTerminal
		num_writers[term] = 0;
		num_waiting[term] = 0;
		writeT_buf_count[term] = 0;
		writeT_buf_length[term] = 0;
		writeT_first_newline[term] = true;

		// Echo
		echo_count[term] = 0;
		input_buf_count[term] = 0;
		screen_len[term] = 0;
		echo_buf_write_index[term] = 0;
		echo_buf_read_index[term] = 0;
		initiate_echo[term] = true;
		first_backspace[term] = true;
		
		// ReadTerminal
		num_readers[term] = 0;
		num_readable_input[term] = 0;
		input_buf_write_index[term] = 0;
		input_buf_read_index[term] = 0;

		// Statistics
		statistics[term].tty_in = 0;
		statistics[term].tty_out = 0;
		statistics[term].user_in = 0;
		statistics[term].user_out = 0;
	}

	return open_terminal[term];
}

/*
 * Initialization needed for the whole terminalsleep(1);
 */
extern
int InitTerminalDriver()
{
	Declare_Monitor_Entry_Procedure();
	int i;
	for (i = 0; i < MAX_NUM_TERMINALS; i++) {
		open_terminal[i] = -1;
		statistics[i].tty_in = -1;
		statistics[i].tty_out = -1;
		statistics[i].user_in = -1;
		statistics[i].user_out = -1;
	}

	return 0;
}

/*
 * Copies internal statistics to the passed statistics pointer
 */
extern
int TerminalDriverStatistics(struct termstat *stats)
{
	Declare_Monitor_Entry_Procedure();
	int i;

	for (i = 0; i < MAX_NUM_TERMINALS; i++) {
		stats[i].tty_in = statistics[i].tty_in;
		stats[i].tty_out = statistics[i].tty_out;
		stats[i].user_in = statistics[i].user_in;
		stats[i].user_out = statistics[i].user_out;
	}

	return 0;
}

/*
 * Called by the hardware once each character typed on the keyboard is ready.
 * It will concatnate the character into the buffer, increment the count
 * and start writing to the terminal.
 */
extern
void ReceiveInterrupt(int term)
{
	Declare_Monitor_Entry_Procedure();

	/* Read from register */
	char c = ReadDataRegister(term);
	statistics[term].tty_in++;

	/* Echo buf update */
	if ((('\b' == c) || ('\177' == c)) && (0 != screen_len[term])) {

		/* Back space if there's room */
		if ((ECHO_BUF_SIZE - echo_count[term]) > 0) {
			echo_buf[term][echo_buf_write_index[term]] = '\b';
			echo_buf_write_index[term] = (echo_buf_write_index[term] + 1) % ECHO_BUF_SIZE;
			echo_count[term]++;
			screen_len[term]--;
		}

	} else if (('\b' != c) && ('\177' != c)) {

		/* Echo only if there's room, else drop the character. */
		if ((ECHO_BUF_SIZE - echo_count[term]) > 1) { // Leave room for beeping
			echo_buf[term][echo_buf_write_index[term]] = c;
			echo_buf_write_index[term] = (echo_buf_write_index[term] + 1) % ECHO_BUF_SIZE;
			echo_count[term]++;
			screen_len[term]++;
		} else if ((ECHO_BUF_SIZE - echo_count[term]) == 1) { // Beep
			echo_buf[term][echo_buf_write_index[term]] = '\7';
			echo_buf_write_index[term] = (echo_buf_write_index[term] + 1) % ECHO_BUF_SIZE;
			echo_count[term]++;
		}
	}

	/* Character processing */
	if ('\r' == c) {
		c = '\n';
	}

	/* Input buf update */
	if (('\b' == c) || ('\177' == c)) {
		if ((input_buf_count[term] > 0) && ('\n' != input_buf[term][(input_buf_write_index[term] + INPUT_BUF_SIZE - 1) % INPUT_BUF_SIZE])) {
			input_buf_write_index[term] = (input_buf_write_index[term] + INPUT_BUF_SIZE - 1) % INPUT_BUF_SIZE;
			input_buf_count[term]--;
		}
	} else {
		if ((INPUT_BUF_SIZE - input_buf_count[term]) > 0) {
			input_buf[term][input_buf_write_index[term]] = c;
			input_buf_write_index[term] = (input_buf_write_index[term] + 1) % INPUT_BUF_SIZE;
			input_buf_count[term]++;
			if ('\n' == c) {
				num_readable_input[term]++;
				CondSignal(toRead[term]);
			}
		} else if ((ECHO_BUF_SIZE - echo_count[term]) > 0) { // Beep
			echo_buf[term][echo_buf_write_index[term]] = '\7';
			echo_buf_write_index[term] = (echo_buf_write_index[term] + 1) % ECHO_BUF_SIZE;
			echo_count[term]++;
		}
	}

	/* If this is the first character, then start the echo */
	if (initiate_echo[term] && (echo_count[term] > 0)) {
		if (num_writers[term] == 0) {
			WriteDataRegister(term, echo_buf[term][echo_buf_read_index[term]]);
			echo_buf_read_index[term] = (echo_buf_read_index[term] + 1) % ECHO_BUF_SIZE;
			echo_count[term]--;
			initiate_echo[term] = false;
		}
	}

}


/*
 * Called by the hardware once each character is written to the 
 * display.
 */
extern
void TransmitInterrupt(int term)
{
	Declare_Monitor_Entry_Procedure();
	int i;
	int prev;

	/* Statistics */
	statistics[term].tty_out++;

	/* Debug */
	//printf("terminal %d: echo_count = %d, screen_len = %d, echo_write = %d, echo_read = %d, [%d, %d, %d]\n", term, echo_count[term], screen_len[term], echo_buf_write_index[term], echo_buf_read_index[term], echo_buf[term][0], echo_buf[term][1], echo_buf[term][2]);
	//fflush(stdout);

	/* Something to write from echo */
	prev = (echo_buf_read_index[term] + ECHO_BUF_SIZE - 1) % ECHO_BUF_SIZE;

	/* Character Processing */
	if ('\r' == echo_buf[term][prev]) {
		WriteDataRegister(term, '\n');
		echo_buf[term][prev] = '\n';
		initiate_echo[term] = false;
		screen_len[term] = 0;
	}

	else if ('\b' == echo_buf[term][prev]) {
		if (first_backspace[term]) {
			WriteDataRegister(term, ' ');
			first_backspace[term] = false;
			initiate_echo[term] = false;
		} else {
			WriteDataRegister(term, '\b');
			echo_buf[term][prev] = '\n';
			first_backspace[term] = true;
			initiate_echo[term] = false;
		}
	}

	/* Keep echoing as long as there is something to echo */
	else if (echo_count[term] > 0) {
		WriteDataRegister(term, echo_buf[term][echo_buf_read_index[term]]);
		echo_buf_read_index[term] = (echo_buf_read_index[term] + 1) % ECHO_BUF_SIZE;
		echo_count[term]--;
		initiate_echo[term] = false;
	}
	
	/* WriteTerminal stuff */
	else if (writeT_buf_count[term] > 0) {
		/* Echo job is done */
		initiate_echo[term] = true;

		writeT_buf_count[term]--;
		statistics[term].user_in++;
	
		/* Keep writing as long as there is something to write */
		if (writeT_buf_count[term] > 0) {
			i = writeT_buf_length[term] - writeT_buf_count[term];
			
			if (writeT_buf[term][i] == '\n' && writeT_first_newline[term]) {
				WriteDataRegister(term, '\r');
				writeT_buf_count[term]++;
				statistics[term].user_in--;
				writeT_first_newline[term] = false;
			} else if (writeT_buf[term][i] == '\n') {
				WriteDataRegister(term, writeT_buf[term][i]);
				screen_len[term] = 0;
				writeT_first_newline[term] = true;
			} else {
				WriteDataRegister(term, writeT_buf[term][i]);
				screen_len[term]++;
				writeT_first_newline[term] = true;
			}
		}
		/* Else the output is done */
		else {
			CondSignal(writing[term]);
		}
	}

	else {
		/* Echo job is done */
		CondSignal(busy_echo[term]);
		initiate_echo[term] = true;
	}
	
}


















