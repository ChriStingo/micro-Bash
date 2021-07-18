#define _DEFAULT_SOURCE
#define _GNU_SOURCE

#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <fcntl.h>
#include <sys/wait.h>
#include "parsing.h"


/**************************************************************************************************************************
Print current directory
**************************************************************************************************************************/
void printCurDir()
{
	char *dir = NULL;
	fprintf(stdout, GREEN "%s" RESET_COLOR "$ ", (dir = get_current_dir_name()));
	free(dir);
}


/**************************************************************************************************************************
Take the input and check if there is a ctrl+D in the first row.
Return 0 if there is a ctrl+D or errors.
**************************************************************************************************************************/
unsigned int inputCommand(char *s)
{
	if (fgets(s, MAXCOMM, stdin) == NULL) {	// insert command
		fprintf(stdout, "^D\n");	// ctrl+D to exit
		return 0;
	}
	return 1;
}


/**************************************************************************************************************************
Function for environment variables.
Return NULL if there is an error, else return the environment variable
**************************************************************************************************************************/
char *environmentVar(char *arg_token)
{
	unsigned int i;
	for (i = 0; i < strlen(arg_token); i++)
		arg_token[i] = toupper(arg_token[i]);	// to upper case (example.: $home = $HOME)
	if ((arg_token = getenv(arg_token + 1)) == NULL){	// insert arguments
		fprintf(stdout, RED "*** Environment variable does not exist ***" RESET_COLOR "\n");
		return NULL;
	}
	return arg_token;
}


/**************************************************************************************************************************
Build in cd command.
Return 0 if there is an error, else 1.
**************************************************************************************************************************/
unsigned int cd(char *dir, unsigned int num_arg)
{
	if (num_arg > 2) {	// arguments error
		fprintf(stdout, RED "micro-bash: cd: too much arguments" RESET_COLOR "\n");
		return 0;
	} else if (num_arg == 1) {	// no arguments
		if (chdir(getenv("HOME")) == -1)
			fprintf(stdout, RED "micro-bash: cd: %s: File or directory doesn't exist" RESET_COLOR "\n", dir);
		return 1;
	}
	if (strcmp(dir, "-") == 0 || strcmp(dir, "~") == 0) {	// "cd -" or "cd ~"
		if (chdir(getenv("HOME")) == -1)
			fprintf(stdout, RED "micro-bash: cd: %s: File or directory doesn't exist" RESET_COLOR "\n", dir);
		return 1;
	}
	if (chdir(dir) == -1) {
		fprintf(stdout, RED "micro-bash: cd: %s: File or directory doesn't exist" RESET_COLOR "\n", dir);
		return 0;
	}
	return 1;
}


/**************************************************************************************************************************
Single command, without pipes.
Return 0 if there is an error, else 1
**************************************************************************************************************************/
unsigned int execSingleCommand(char **arg_token, int num_arg, int fd_in, int fd_out)
{
	pid_t child_pid;
	arg_token = (char **)realloc(arg_token, sizeof(char *) * (num_arg + 1));
	arg_token[num_arg] = NULL;
	if ((child_pid = fork()) == -1)
		return 0;
	if (child_pid == 0) {	// Child process
		if (fd_in >= 0)
			if (dup2(fd_in, STDIN_FILENO) == -1) {	// redirect input
				perror("Error dup2 for input redirect\n");
				return 0;
			}
		if (fd_out >= 0)
			if (dup2(fd_out, STDOUT_FILENO) == -1) {	// redirect output
				perror("Error dup2 for output redirect\n");
				return 0;
			}
		execvp(arg_token[0], arg_token);	// execute command
		// execvp failed
		fprintf(stdout, RED "*** BAD COMMAND!!! ***" RESET_COLOR "\n");
		exit(EXIT_FAILURE);
	} else {
		// father process
		if (wait(NULL) == -1) {
			free(arg_token);
			return 0;
		}
		free(arg_token);
	}
	return 1;
}


/**************************************************************************************************************************
Redirect input.
Return -1 is there is an error, else return the file descriptor
**************************************************************************************************************************/
int openRedirInput(char *arg_token)
{
	int fd_in = -2;
	if ((fd_in = open(arg_token + 1, O_RDONLY)) < 0) {
		fprintf(stdout, RED "micro-bash: %s: File or directory doesn't exist" RESET_COLOR "\n", arg_token + 1);
		return -1;
	}
	return fd_in;
}


/**************************************************************************************************************************
Redirect output.
Return -1 is there is an error, else return the file descriptor
**************************************************************************************************************************/
int openRedirOutput(char *arg_token)
{
	int fd_out = -2;
	if ((fd_out = open(arg_token + 1, O_TRUNC | O_CREAT | O_RDWR, 0666)) < 0) {
		fprintf(stdout, RED "micro-bash: Error opening file to redirect output" RESET_COLOR "\n");
		return -1;
	}
	return fd_out;
}


/**************************************************************************************************************************
Wait for each child of father process and check if a child is interrupted with status != 0
Return 0 if there is an error, else 1
**************************************************************************************************************************/
unsigned int wait_children_inPipe(int numPipes, int *status, int *pid)
{
	for (int i = 0; i < numPipes + 1; i++) {
		if (wait(status) == -1) {
			return 0;
		}
		if (WIFEXITED(*status) && WEXITSTATUS(*status) != 0)
			fprintf(stdout, LIGHT_BLUE "Process with pid %d ends with status %d" RESET_COLOR "\n", *pid, WEXITSTATUS(*status));
	}
	return 1;
}


/**************************************************************************************************************************
Close file descriptor and reset input/output
Retrurn 0 if there is an error, else 1
**************************************************************************************************************************/
unsigned int close_pipe(unsigned int *stdin_safe, unsigned int *stdout_safe, int numPipes, int *pipefds)
{
	for (int i = 0; i < 2 * numPipes; i++)
		if (close(pipefds[i]) == -1)
			break;
	free(pipefds);
	if (dup2(*stdin_safe, 0) == -1) {	// reset input
		perror("Error in dup2\n");
		return 0;
	}
	if (dup2(*stdout_safe, 1) == -1) {	// reset output
		perror("Error in dup2\n");
		return 0;
	}
	if (close(*stdout_safe) == -1)
		return 0;
	if (close(*stdin_safe) == -1)
		return 0;
	return 1;
}


/**************************************************************************************************************************
Check if:
 - ">" and black space;
 - more commands after ">file.extension";
 - "<" in right place.
Return 0 if there is an error, else 1
**************************************************************************************************************************/
unsigned int checkErrorPipedCommand(queue * q)
{
	queue q2;
	// Create and copy of aux queue
	create(&q2, size(q));
	copyQueue(q, &q2);
	
	char * s1 = NULL;
	do{						// take first command (command to pipe or empty queue)
		if (isEmpty(&q2))
			break;
		s1 = dequeue(&q2);	// take first element from queue
	} while (strcmp(s1, "|") != 0); // untile i have a pipe
	while (!isEmpty(&q2)){
		s1 = dequeue(&q2);		// take the second command
		if (s1[0] == '>'){	
			if (strlen(s1) == 1) {	// ">" and a space is an error
				fprintf(stdout, RED "*** BAD COMMAND!!! ***" RESET_COLOR "\n");
				reset(&q2);
				return 0;
			}
			if (!isEmpty(&q2)) {	// more commands after ">file.extension" is an error
				fprintf(stdout, RED "*** BAD COMMAND!!! ***" RESET_COLOR "\n");
				reset(&q2);
				return 0;
			}
		}
		if (s1[0] == '<') {	// error because '<' only on first command
			fprintf(stdout, RED "*** BAD COMMAND!!! ***" RESET_COLOR "\n");
			reset(&q2);
			return 0;
		}
	}
	reset(&q2);
	return 1;
}


/**************************************************************************************************************************
Execute commands with pipe
Return 0 if there is an error, else 1
**************************************************************************************************************************/
unsigned int runPipedCommands(queue * q, char **command, int n_arg, int numPipes)
{
	int status, *pipefds;
	unsigned int i, first = 0, j = 0;
	pid_t pid;
	unsigned int redirect_Out = 0;	// 0 false - 1 true
	int std_save;		// for ">" and "<"
	unsigned int stdin_safe = dup(STDIN_FILENO);	// save stdin
	unsigned int stdout_safe = dup(STDOUT_FILENO);	// save stdout
	pipefds = (int *)malloc(sizeof(int) * (2 * numPipes));
	for (i = 0; i < numPipes; i++)
		if (pipe(pipefds + i * 2) == -1) {
			perror("Errore in pipe\n");
			close_pipe(&stdin_safe, &stdout_safe, numPipes, pipefds);
			free(command);
			return 0;
		}
	// check "<"
	for (int k = 0; k < n_arg - 1; k++)	// check if bad positions
		if (command[k][0] == '<') {
			fprintf(stdout, RED "*** BAD COMMAND!!! ***" RESET_COLOR "\n");
			close_pipe(&stdin_safe, &stdout_safe, numPipes, pipefds);
			free(command);
			return 0;
		}
	// check if there is to change the stdin
	if (command[n_arg - 1][0] == '<') {
		if (strlen(command[n_arg - 1]) == 1) {
			fprintf(stdout, RED "*** BAD COMMAND!!! ***" RESET_COLOR "\n");
			close_pipe(&stdin_safe, &stdout_safe, numPipes, pipefds);
			return 0;
		}
		if ((std_save = openRedirInput(command[n_arg - 1])) == -1) {
			free(pipefds);
			free(command);
			return 0;
		}
		n_arg--;
		// if there is to change stdin
		if (dup2(std_save, 0) == -1) {
			perror("Error dup2 for output redirect in file\n");
			close(std_save);
			free(pipefds);
			free(command);
			return 0;
		}
		if (close(std_save) == -1) {
			free(pipefds);
			free(command);
			return 0;
		}
	}


	while (!isEmpty(q)) {
		
		while (!isEmpty(q) && j > 0) {
			command = (char **)realloc(command, sizeof(char *) * (n_arg + 1));
			char *singleArg;
			singleArg = dequeue(q);
			if (singleArg[0] == '|'){	// after pipe no more commands	
				break;
			}

			// check ">"
			if (singleArg[0] == '>') {
				redirect_Out = 1;
				// take new stdout
				if ((std_save = openRedirOutput(singleArg)) == -1) {
					wait_children_inPipe(numPipes, &status, &pid);
					close_pipe(&stdin_safe, &stdout_safe, numPipes, pipefds);
					free(command);
					return 0;
				}
				break;
			}
			command[n_arg] = singleArg;
			n_arg++;
		}
		command = (char **)realloc(command, sizeof(char *) * (n_arg + 1));
		command[n_arg] = NULL;	// for execvp
		pid = fork();
		if (pid == 0) {	// child process
			// output
			if (redirect_Out == 1) {
				// if there is to change stdout
				if (dup2(std_save, 1) == -1) {
					perror("Error dup2 for output redirect in file\n");
					close_pipe(&stdin_safe, &stdout_safe, numPipes, pipefds);
					free(command);
					return 0;
				}
				if (close(std_save) == -1) {
					free(command);
					free(pipefds);
					return 0;
				}
			} else {
				// if it isn't the last command
				if (j < 2 * numPipes) {
					if (dup2(pipefds[j + 1], 1) == -1) {
						perror("Error dup2 output - PIPE\n");
						close_pipe(&stdin_safe, &stdout_safe, numPipes, pipefds);
						free(command);
						return 0;
					}
				}
			}

			// input
			// if i'm not in the first command
			if (j != 0) {
				if (dup2(pipefds[j - 2], 0) == -1) {
					perror("Error dup2 input - PIPE\n");
					close_pipe(&stdin_safe, &stdout_safe, numPipes, pipefds);
					free(command);
					return 0;
				}
			}

			// close all file descriptor
			for (i = 0; i < 2 * numPipes; i++)
				if (close(pipefds[i]) == -1)
					break;

			// execute instruction
			if (execvp(command[first], command + first) == -1) {
				fprintf(stdout, RED "*** BAD COMMAND!!! *** - Error of: %s" RESET_COLOR "\n", command[first]);
				close_pipe(&stdin_safe, &stdout_safe, numPipes, pipefds);
				free(command);
				exit(EXIT_FAILURE);
			}

		} else if (pid < 0) {	// if fork return error
			perror("Errore fork in pipe\n");
			free(pipefds);
			free(command);
			return 0;
		}
		// father process
		j += 2;
		first = n_arg;
	}

	// closing all file descriptor opened by pipe
	for (i = 0; i < 2 * numPipes; i++)
		if (close(pipefds[i]) == -1)
			break;
	// wait for each child and check if someone failed, close pipe and reset stdin and stdout
	if (!wait_children_inPipe(numPipes, &status, &pid)) {
		free(pipefds);
		free(command);
		return 0;
	}
	if (!close_pipe(&stdin_safe, &stdout_safe, numPipes, pipefds)) {
		free(command);
		return 0;
	}
	free(command);
	return 1;
}


/**************************************************************************************************************************
Parse command and call function for single command, redirections and pipe.
Return 0 if there is an error, else 1.
**************************************************************************************************************************/
unsigned int execCommand(queue * q, unsigned int num_pipe)
{
	char *singleArg, *singleArg2;
	int n_arg = 0, n_comm = 0;
	char **commArray = NULL;
	if (isEmpty(q))	// no commands
		return 0;
	while (!isEmpty(q)) {
		commArray = (char **)realloc(commArray, sizeof(char *) * (n_arg + 1));
		singleArg = dequeue(q);	// take arguments
		if (strcmp(singleArg, "|") == 0) {	// check pipe
			if (n_arg == 0) {	// no pipe in the first element
				fprintf(stdout, RED "*** BAD COMMAND!!! ***" RESET_COLOR "\n");
				free(commArray);
				return 0;
			}
			n_comm++;
			if (strcmp(commArray[0], "cd") == 0) {	// if cd and pipe
				fprintf(stdout, RED "*** BAD COMMAND!!! ***" RESET_COLOR "\n");
				free(commArray);
				return 0;
			}
			if (!runPipedCommands(q, commArray, n_arg, num_pipe))	// execute pipe
				return 0;
			return 1;
		} else if (singleArg[0] == '<' && !isEmpty(q) && num_pipe == 0) {	// if "<" then ">"
			if (n_arg == 0) {
				fprintf(stdout, RED "*** BAD COMMAND!!! ***" RESET_COLOR "\n");
				free(commArray);
				return 0;
			}
			n_comm++;
			singleArg2 = dequeue(q);
			if (!isEmpty(q)) {	// if more after "<file.extension >file.extension" it is an error
				fprintf(stdout, RED "*** BAD COMMAND!!! ***" RESET_COLOR "\n");
				free(commArray);
				return 0;
			}
			if (singleArg2[0] == '>') {	// if ">"
				n_comm++;
				if (n_comm > 2) {
					fprintf(stdout, RED "*** BAD COMMAND!!! ***" RESET_COLOR "\n");
					free(commArray);
					return 0;
				}
				if (strcmp(commArray[0], "cd") == 0) {	// if cd i have an error
					fprintf(stdout, RED "*** BAD COMMAND!!! ***" RESET_COLOR "\n");
					free(commArray);
					return 0;
				}
				int fd_out;	// change output
				if ((fd_out = openRedirOutput(singleArg2)) == -1) {
					free(commArray);
					return 0;
				}
				int fd_in;	// change input
				if ((fd_in = openRedirInput(singleArg)) == -1) {
					free(commArray);
					return 0;
				}
				pid_t child_pid;
				commArray[n_arg] = NULL;	// for execvp
				if ((child_pid = fork()) == -1) {
					free(commArray);
					return 0;
				}
				if (child_pid == 0) {	// child process
					if (dup2(fd_in, STDIN_FILENO) == -1) {
						perror("Error dup2 for input redirect\n");
						free(commArray);
						return 0;
					}
					if (dup2(fd_out, STDOUT_FILENO) == -1) {
						perror("Error dup2 for output redirect\n");
						free(commArray);
						return 0;
					}
					execvp(commArray[0], commArray);	// execute command
					fprintf(stdout, RED "*** BAD COMMAND!!! ***" RESET_COLOR "\n");
					free(commArray);
					exit(EXIT_FAILURE);
				} else {	// father process
					if (wait(NULL) == -1) {
						free(commArray);
						return 0;
					}
					if (close(fd_in) == -1) {
						free(commArray);
						return 0;
					}
					if (close(fd_out) == -1) {
						free(commArray);
						return 0;
					}
					free(commArray);
				}
			} else {
				fprintf(stdout, RED "*** BAD COMMAND!!! ***" RESET_COLOR "\n");
				free(commArray);
				return 0;
			}
			return 1;
		} else if (singleArg[0] == '>' && !isEmpty(q) && num_pipe == 0) {	// if ">" then "<"
			if (n_arg == 0) {
				fprintf(stdout, RED "*** BAD COMMAND!!! ***" RESET_COLOR "\n");
				free(commArray);
				return 0;
			}
			n_comm++;
			singleArg2 = dequeue(q);
			if (!isEmpty(q)) {	// if ">file.extension <file.extensione" there is an error
				fprintf(stdout, RED "*** BAD COMMAND!!! ***" RESET_COLOR "\n");
				free(commArray);
				return 0;
			}
			if (singleArg2[0] == '<') {	// "<"  
				n_comm++;
				if (n_comm > 2) {
					fprintf(stdout, RED "*** BAD COMMAND!!! ***" RESET_COLOR "\n");
					free(commArray);
					return 0;
				}
				if (strcmp(commArray[0], "cd") == 0) {	// if cd there is an error
					fprintf(stdout, RED "*** BAD COMMAND!!! ***" RESET_COLOR "\n");
					free(commArray);
					return 0;
				}
				int fd_out;	// change output
				if ((fd_out = openRedirOutput(singleArg)) == -1) {
					free(commArray);
					return 0;
				}
				int fd_in;	// change input
				if ((fd_in = openRedirInput(singleArg2)) == -1) {
					free(commArray);
					return 0;
				}
				pid_t child_pid;
				commArray[n_arg] = NULL;	// for execvp
				if ((child_pid = fork()) == -1) {
					free(commArray);
					return 0;
				}
				if (child_pid == 0) {	// child process
					if (dup2(fd_in, STDIN_FILENO) == -1) {
						perror("Error dup2 for input redirect\n");
						free(commArray);
						return 0;
					}
					if (dup2(fd_out, STDOUT_FILENO) == -1) {
						perror("Error dup2 for output redirect\n");
						free(commArray);
						return 0;
					}
					execvp(commArray[0], commArray);	// execute command
					fprintf(stdout, RED "*** BAD COMMAND!!! ***" RESET_COLOR "\n");
					free(commArray);
					exit(EXIT_FAILURE);
				} else {	// father process
					if (wait(NULL) == -1) {
						free(commArray);
						return 0;
					}
					if (close(fd_in) == -1) {
						free(commArray);
						return 0;
					}
					if (close(fd_out) == -1) {
						free(commArray);
						return 0;
					}
					free(commArray);
				}
			} else {
				fprintf(stdout, RED "*** BAD COMMAND!!! ***" RESET_COLOR "\n");
				free(commArray);
				return 0;
			}
			return 1;
		} else if (singleArg[0] == '<' && isEmpty(q)) {	// redirect input
			if (n_arg == 0) {
				fprintf(stdout, RED "*** BAD COMMAND!!! ***" RESET_COLOR "\n");
				free(commArray);
				return 0;
			}
			n_comm++;
			if (n_comm > 2) {
				fprintf(stdout, RED "*** BAD COMMAND!!! ***" RESET_COLOR "\n");
				free(commArray);
				return 0;
			}
			if (strcmp(commArray[0], "cd") == 0) {	// if cd
				fprintf(stdout, RED "*** BAD COMMAND!!! ***" RESET_COLOR "\n");
				free(commArray);
				return 0;
			}
			if (n_comm != 1) {	// if "<" in the first command
				fprintf(stdout, RED "*** BAD redirect input ***" RESET_COLOR "\n");
				free(commArray);
				return 0;
			}
			int fd_in;	// change input
			if ((fd_in = openRedirInput(singleArg)) == -1) {
				free(commArray);
				return 0;
			}
			if (!execSingleCommand(commArray, n_arg, fd_in, -2)) {	// redirect input
				close(fd_in);
				free(commArray);
				return 0;
			}
			if (close(fd_in) == -1) {
				free(commArray);
				return 0;
			}
			return 1;
		} else if (singleArg[0] == '>') {	// redirect output
			if (n_arg == 0) {
				fprintf(stdout, RED "*** BAD COMMAND!!! ***" RESET_COLOR "\n");
				free(commArray);
				return 0;
			}
			n_comm++;
			if (strcmp(commArray[0], "cd") == 0) {	// cd
				fprintf(stdout, RED "*** BAD COMMAND!!! ***" RESET_COLOR "\n");
				free(commArray);
				return 0;
			}
			if (!isEmpty(q)) {	// check if ">" is last command
				fprintf(stdout, RED "*** BAD redirect output ***" RESET_COLOR "\n");
				free(commArray);
				return 0;
			}
			int fd_out;	// change output
			if ((fd_out = openRedirOutput(singleArg)) == -1) {
				free(commArray);
				return 0;
			}
			if (!execSingleCommand(commArray, n_arg, -2, fd_out)) {	// redirect output
				close(fd_out);
				free(commArray);
				return 0;
			}
			if (close(fd_out) == -1) {
				free(commArray);
				return 0;
			}
			return 1;
		}
		commArray[n_arg] = singleArg;
		n_arg++;
	}
	// no "|" or "<" or ">" execute single command
	if (strcmp(commArray[0], "cd") == 0) {	// if cd
		if (n_arg == 1){
			if (!cd(NULL, n_arg)) {
				free(commArray);
				return 0;	// fail
			}
		} else {
			if (!cd(commArray[1], n_arg)) {
				free(commArray);
				return 0;	// fail
			}
		}
		free(commArray);
	} else {
		if (!execSingleCommand(commArray, n_arg, -2, -2)) {	// single command
			free(commArray);
			return 0;
		}
	}
	return 1;
}


/**************************************************************************************************************************
Parse input string.
Return 0 if there is an error, else 1.
**************************************************************************************************************************/
unsigned int parser(char *complete_comm, queue * q)
{
	unsigned int num_pipe = 0, i;
	char *comm_token, *arg_token;
	complete_comm[strlen(complete_comm) - 1] = 0;	// don't take \n in last position
	for (i = 0; i < strlen(complete_comm); i++)	// remove tab
		if (complete_comm[i] == '\t')
			complete_comm[i] = ' ';
	if(complete_comm[0] == '|' || complete_comm[strlen(complete_comm)-1] == '|'){
		fprintf(stdout, RED "*** BAD COMMAND!!! ***" RESET_COLOR "\n");
		return 0;
	}
	while ((comm_token = strtok_r(complete_comm, "|", &complete_comm))) {	// divide for "|"
		while ((arg_token = strtok_r(comm_token, " ", &comm_token))) {	// divide for spaces
			if (arg_token[0] == '$') {	// if i have a '$'
				if ((arg_token = environmentVar(arg_token)) == NULL)
					return 0;
			}
			
			enqueue(q, arg_token);
		}
		if (strlen(complete_comm) > 0) {	// pipe
			enqueue(q, "|");
			num_pipe++;
		}
		if (!checkErrorPipedCommand(q))	// redirect errors
			return 0;
	}
		
	if (checkPipeError(q)) {	// more pipes errors
		fprintf(stdout, RED "*** BAD COMMAND!!! ***" RESET_COLOR "\n");
		return 0;
	}
	if (!execCommand(q, num_pipe))	// execute command
		return 0;
	return 1;
}
