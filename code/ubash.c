#include "parsing.h"


/**************************************************************************************************************************
Main.
Return 0 if there is an error, else 1
**************************************************************************************************************************/
int main(int argc, char **argv)
{
	char comm[MAXCHARCOMM];
	queue q;
	printf("\n##### uBASH - Laboratorio 2 di SET(i) 2019/2020 #####\n\n");
	while (1) {
		printCurDir();
		if (!inputCommand(comm))	// take input and check if it's ctrl+D
			return 0;
		if (comm[0] == '\n')	// if the user insert an '\n' before first input
			continue;
		create(&q, MAXQUEUEELEM);
		if (!parser(comm, &q)) {	// execute the parser
			reset(&q);
			continue;
		}
		reset(&q);
	}
	return 1;
}
