#include <stdio.h>
#include <stdlib.h>
#include <threads.h>
#include <unistd.h>
#include <terminals.h>
#include <hardware.h>
void writer(void *);
void writer2(void *);
void reader(void *);
void reader2(void *);

char string[] = "abcdefghijklmnopqrstuvwxyz\n";
int length = sizeof(string) - 1;

int main(int argc, char **argv)
{
    InitTerminalDriver();
    InitTerminal(1);
    InitTerminal(2);
    if (argc > 1) HardwareOutputSpeed(1, atoi(argv[1]));
    if (argc > 2) HardwareInputSpeed(1, atoi(argv[2]));
    if (argc > 1) HardwareOutputSpeed(2, atoi(argv[1]));
    if (argc > 2) HardwareInputSpeed(2, atoi(argv[2]));
    sleep(5);

        while(1) {
	  ThreadCreate(writer, NULL);
	  ThreadCreate(reader, NULL);
          ThreadCreate(writer2, NULL);
	  ThreadCreate(reader2, NULL);
        sleep(5);
	}
    ThreadWaitAll();

    exit(0);
}

void
writer(void *arg)
{
    int status;

    //    printf("Doing WriteTerminal... '\n");
    fflush(stdout);
    status = WriteTerminal(1, string, length);
    char* str = malloc(3);
    // status = ReadTerminal(1, str,3);
    printf("'. Done: status = %d.\n", status);
    fflush(stdout);
}

void
writer2(void *arg)
{
    int status;

    printf("Doing WriteTerminal... '");
    fflush(stdout);
    status = WriteTerminal(2, "foofoofoo\n",10);

    printf("'. Done: status = %d.\n", status);
    fflush(stdout);
}

void
reader(void *arg)
{
    int status;

    //    printf("Doing ReadTerminal... '");
    fflush(stdout);
    char* str = malloc(100);
    status = ReadTerminal(1, str,10);
    printf("read1 %s", str);
    free(str);
    fflush(stdout);
}
void
reader2(void *arg)
{
    int status;

    printf("Doing ReadTerminal... '");
    fflush(stdout);
    char* str = malloc(100);
    status = ReadTerminal(2, str,100);
    printf("read2 %s", str);
    fflush(stdout);
}
