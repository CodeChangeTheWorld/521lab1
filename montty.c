//
// Created by Liu Fang on 2/7/17.
//

#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <threads.h>
#include <hardware.h>
#include <terminals.h>

#define ECHO_BUF_SIZE 1024
#define INPUT_BUF_SIZE 4096

//Define condition variable
static cond_id_t writer[NUM_TERMINALS];
static cond_id_t writing[NUM_TERMINALS];
static cond_id_t reader[NUM_TERMINALS];
static cond_id_t readable[NUM_TERMINALS];
static cond_id_t echobusy[NUM_TERMINALS];


//Define echo buffer and input buffer for all terminals

char echo_buffer[NUM_TERMINALS][ECHO_BUF_SIZE];
char input_buffer[NUM_TERMINALS][INPUT_BUF_SIZE];

//WriteTerminal
int num_writer[NUM_TERMINALS] ;
int num_waiting[NUM_TERMINALS];
char *writeT_buf[NUM_TERMINALS];
int writeT_buf_count[NUM_TERMINALS] ;
int writeT_buf_length[NUM_TERMINALS] ;
bool writeT_first_newline[NUM_TERMINALS] ;// check if current char is the first in a line

//ReadTerminal
int num_reader[NUM_TERMINALS] ;
int num_readable_input[NUM_TERMINALS];
int input_buf_write_index[NUM_TERMINALS];
int input_buf_read_index[NUM_TERMINALS] ;

//Echo
int echo_count[NUM_TERMINALS] ;
int input_buf_count[NUM_TERMINALS];
int screen_len[NUM_TERMINALS] ; // track char nums on screen
int echo_buf_write_index[NUM_TERMINALS] ; // track current index for echo_buffer write
int echo_buf_read_index[NUM_TERMINALS] ; // track current index for echo_buffer read
bool initiate_echo[NUM_TERMINALS] ; // check
bool first_backspace[NUM_TERMINALS];

//Statistics
struct termstat statistics[NUM_TERMINALS];

//track open terminals
int open_terminals[NUM_TERMINALS];


extern
int WriteTerminal(int term, char *buf, int buflen)
{
    Declare_Monitor_Entry_Procedure();
    //precheck of exception
    if(buflen<1 || open_terminals[term] <0 || term<0 || term > NUM_TERMINALS-1) return -1;

    //check if anyone else is writing terminal:term
    if(num_writer[term] > 0 || num_waiting[term] >0 ){
        //num_waiting avoid starvation. As long as there are other writer waiting to write, I will wait after them.
        num_waiting[term]++;
        CondWait(writer[term]);
    }else{
        num_waiting[term]++;
    }

    num_writer[term]++;
    num_waiting[term]--;

    if(!initiate_echo[term])
        CondWait(echobusy[term]);

    writeT_buf[term] = buf;
    writeT_buf_count[term] = buflen;
    writeT_buf_length[term] = buflen;

    if(writeT_buf[term][0]=='\n'){
        WriteDataRegister(term, '\r');
        writeT_buf_count[term]++;
        statistics[term].user_in--;
        writeT_first_newline[term]=false;
    }else{
        WriteDataRegister(term, writeT_buf[term][0]); //put a char into write data register pf terminal term
        writeT_first_newline[term]= true;
    }

    CondWait(writing[term]);
    num_writer[term]--;
    CondSignal(writer[term]);
    return buflen;
}

extern
int ReadTerminal(int term, char *buf,int buflen)
{
    // copy characters typed from terminal to buffer *buf
    Declare_Monitor_Entry_Procedure();
    char c;
    int i;

    if((buflen<1) || buflen> INPUT_BUF_SIZE || (open_terminals[term] < 0) || term <0 || term > NUM_TERMINALS-1) return -1;

    if(num_reader[term]>0){
        CondWait(reader[term]);
    }
    num_reader[term]++;

    if(num_readable_input[term]==0)
        CondWait(readable[term]);
    num_readable_input[term]--;

    //read from input buffer
    for(i=0; i<buflen;){
        c = input_buffer[term][input_buf_read_index[term]];
        input_buf_read_index[term] = (input_buf_read_index[term]+1)%INPUT_BUF_SIZE;
        strncat(buf, &c, 1);

        statistics[term].user_out++;
        i++;
        input_buf_count[term]--;
        if('\n' == c) break;
    }

    if('\n' != c) num_readable_input[term]++;
    num_reader[term]--;
    CondSignal(reader[term]);
    return i;
}


extern
int InitTerminal(int term){
    Declare_Monitor_Entry_Procedure();

    //precheck
    if(open_terminals[term] == 0 || term < 0 || term > NUM_TERMINALS-1) return -1;


    open_terminals[term] = InitHardware(term);

    if(open_terminals[term] == 0){

        //conditional variable
        writer[term] = CondCreate();
        writing[term] = CondCreate();
        echobusy[term] = CondCreate();
        reader[term] = CondCreate();
        readable[term] = CondCreate();

        //WriteTerminal
        num_writer[term] = 0 ;
        num_waiting[term]  = 0 ;
        writeT_buf_count[term] =0;
        writeT_buf_length[term] = 0;
        writeT_first_newline[term] = true;

        //ReadTerminal
        num_reader[term] = 0;
        num_readable_input[term] =0;
        input_buf_write_index[term] =0;
        input_buf_read_index[term] =0;

        //Echo
        echo_count[term] =0;
        input_buf_count[term] =0;
        screen_len[term] =0;
        echo_buf_write_index[term] = 0;
        echo_buf_read_index[term] = 0;
        initiate_echo[term] = true;
        first_backspace[term] = true;

        //Statistics
        statistics[term].tty_in =0;
        statistics[term].tty_out =0;
        statistics[term].user_in =0;
        statistics[term].user_out =0;
    }

    return open_terminals[term];
}

extern
int InitTerminalDriver(){
    Declare_Monitor_Entry_Procedure();
    int i;
    for(i=0;i<NUM_TERMINALS;i++){
        open_terminals[i] = -1;
        statistics[i].tty_in = -1;
        statistics[i].tty_out =-1;
        statistics[i].user_in =-1;
        statistics[i].user_out =-1;

    }
    return 0;
}

extern
int TerminalDriverStatistics(struct termstat *stats){
    Declare_Monitor_Entry_Procedure();
    int i;
    for(i=0;i<NUM_TERMINALS;i++){
        stats[i].tty_in = statistics[i].tty_in;
        stats[i].tty_out = statistics[i].tty_out;
        stats[i].user_in = statistics[i].user_in;
        stats[i].user_out = statistics[i].user_out;
    }
    return 0;
}

extern
void ReceiveInterrupt(int term){
    //read from ReadDataRegister to echo buffer
    Declare_Monitor_Entry_Procedure();

    //Read from register
    char c = ReadDataRegister(term);
    statistics[term].tty_in++;

    //Echo buffer update
    if(('\b' == c || '\177' == c) && 0!= screen_len[term]){

        if(ECHO_BUF_SIZE - echo_count[term] > 0){
            echo_buffer[term][echo_buf_write_index[term]] = '\b';
            echo_buf_write_index[term] = (echo_buf_write_index[term]+1)%ECHO_BUF_SIZE;
            echo_count[term]++;
            screen_len[term]--;
        }
    }else if(('\b' !=c)&&('\177' != c)){
        if(ECHO_BUF_SIZE - echo_count[term] > 1){
            echo_buffer[term][echo_buf_write_index[term]] = c;
            echo_buf_write_index[term] = (echo_buf_write_index[term]+1)%ECHO_BUF_SIZE;
            echo_count[term]++;
            screen_len[term]++;
        }else if(ECHO_BUF_SIZE-echo_count[term] == 1){ //when the buffer is full, beep
            echo_buffer[term][echo_buf_write_index[term]] = '\7';
            echo_buf_write_index[term] = (echo_buf_write_index[term]+1)%ECHO_BUF_SIZE;
            echo_count[term]++;
        }
    }


    if('\r' == c){
        c = '\n';
    }

    //Input buffer update
    if(('\b' == c)||('\177' == c)){
        //check if it is the new line
        if(input_buf_count[term]>0 && ('\n' != input_buffer[term][(input_buf_write_index[term]+ INPUT_BUF_SIZE-1)%INPUT_BUF_SIZE])){
            input_buf_write_index[term] = (input_buf_write_index[term] + INPUT_BUF_SIZE -1 )%INPUT_BUF_SIZE;
            input_buf_count[term]--;
        }
    }else{
        if(INPUT_BUF_SIZE - input_buf_count[term] > 0){
            input_buffer[term][input_buf_write_index[term]] = c;
            input_buf_write_index[term] = (input_buf_write_index[term]+1)%INPUT_BUF_SIZE;
            input_buf_count[term]++;
            if('\n' == c){
                num_readable_input[term]++;
                CondSignal(readable[term]);
            }
        }else if(ECHO_BUF_SIZE - echo_count[term] > 0){
            echo_buffer[term][echo_buf_write_index[term]] ='\7';
            echo_buf_write_index[term] = (echo_buf_write_index[term]+1)%ECHO_BUF_SIZE;
            echo_count[term]++;
        }
    }

    if(initiate_echo[term] && (echo_count[term]>0)){
        if(num_writer[term] == 0){
            WriteDataRegister(term, echo_buffer[term][echo_buf_read_index[term]]);
            echo_buf_read_index[term] = (echo_buf_read_index[term] +1 )%ECHO_BUF_SIZE;
            echo_count[term]--;
            initiate_echo[term]=false;
        }
    }
}

extern
void TransmitInterrupt(int term) {
    Declare_Monitor_Entry_Procedure();
    int i;
    int prev;

    statistics[term].tty_out++;
    prev = (echo_buf_read_index[term] + ECHO_BUF_SIZE - 1)% ECHO_BUF_SIZE;

    if('\r' == echo_buffer[term][prev]){
        WriteDataRegister(term, '\n');
        echo_buffer[term][prev]='\n';
        initiate_echo[term] = false;
        screen_len[term] = 0;
    } else if('\b' == echo_buffer[term][prev]){
        if(first_backspace[term]){
            WriteDataRegister(term, ' ');
            first_backspace[term]= false;
            initiate_echo[term] = false;
        }else{
            WriteDataRegister(term, '\b');
            echo_buffer[term][prev]='\n';
            first_backspace[term] = true;
            initiate_echo[term]=false;
        }
    }
    else if(echo_count[term] > 0){
        WriteDataRegister(term,echo_buffer[term][echo_buf_read_index[term]]);
        echo_buf_read_index[term] = (echo_buf_read_index[term] + 1)%ECHO_BUF_SIZE;
        echo_count[term]--;
        initiate_echo[term]=false;
    }
    else if(writeT_buf_count[term]>0){
        initiate_echo[term] = true;
        writeT_buf_count[term]--;
        statistics[term].user_in++;

        //Keep writing as long as there is sth to write
        if(writeT_buf_count[term] >0 ){
            i = writeT_buf_length[term] - writeT_buf_count[term];
            if(writeT_buf[term][i] == '\n' && writeT_first_newline[term]){
                WriteDataRegister(term, '\r');
                writeT_buf_count[term]++;
                statistics[term].user_in--;
                writeT_first_newline[term] = false;
            }else if(writeT_buf[term][i]=='\n'){
                WriteDataRegister(term, writeT_buf[term][i]);
                screen_len[term] =0;
                writeT_first_newline[term] = true;
            }else{
                WriteDataRegister(term, writeT_buf[term][i]);
                screen_len[term]++;
                writeT_first_newline[term] = true;
            }
        }else{
            CondSignal(writing[term]);
        }
    }

    else{
        CondSignal(echobusy[term]);
        initiate_echo[term] = true;
    }
}
