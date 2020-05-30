#include<stdio.h>
#include<stdlib.h>
#include<string.h>
#include<dirent.h>
#include<fcntl.h>
#include<sys/types.h>
#include<unistd.h>
#include<signal.h>

#define CMD_LEN 2048
#define MAX_ARGS 512

int fgMode = 0; //global flag for foreground-only mode
int changeMode = 0; //indicates if there was a change from foreground-only mode status
			//0: no change, 1: entering FG only mode 2: exiting FG only mode

void catchSIGTSTP(int signo){
	if(!fgMode){
		changeMode = 1;
		fgMode = 1;
	}
	else{
		changeMode = 2;
		fgMode = 0;
	}
}


int main(){

	char command[CMD_LEN];	//user entered command
	char *argv[MAX_ARGS];//tokenized arguments from command
	int argc;		//number of arguments
	char* line;
	size_t n;
	char sourceFN[120];
	char destFN[120];
	pid_t childPID = -5;
	pid_t bgChildren[128]; //stores PID of background child processes
	int bgCh = 0; //counts number of background children
	int childExitMethod = 0;
	int redirin, redirout; //stores index of redirection symbols
	int bg = 0; //boolean value for run as background
	int i; //loop counter


	while(1){

		//set SIGTSTP to turn foreground-only mode on and off
		struct sigaction SIGTSTP_act = {0}; //initialize empty struct for handling sigints
		SIGTSTP_act.sa_handler = catchSIGTSTP; //sets action to be function catchSIGTSTP
		sigfillset(&SIGTSTP_act.sa_mask); //delay all other signals during handler execution
		SIGTSTP_act.sa_flags = SA_RESTART;
		sigaction(SIGTSTP, &SIGTSTP_act, NULL); //register signal handler for SIGTSTP/ctrl+Z

		//set parent process to ignore SIGINT
		struct sigaction SIGINT_ignore = {0}; //initialize empty struct for handling sigints
		SIGINT_ignore.sa_handler = SIG_IGN;
		sigfillset(&SIGINT_ignore.sa_mask); //delay all other signals during handler execution
		SIGINT_ignore.sa_flags = SA_RESTART;
		sigaction(SIGINT, &SIGINT_ignore, NULL); //register signal handler for SIGINT/ctrl+C

		if(bg > 0){
			printf("background pid is %d\n", childPID);
		}
		bg = 0;

		//print message if entering or exiting foreground-only mode
		if(changeMode == 1){
			printf("Entering foreground-only mode (& is now ignored)\n");
			fflush(stdout);
		}
		else if (changeMode == 2){
			printf("(Exiting foreground-only mode)\n");
			fflush(stdout);
		}
		changeMode = 0; //reset flag

		//print message of a child process was killed by a signal
		if(WIFSIGNALED(childExitMethod) != 0){
			printf("terminated by signal %d\n", WTERMSIG(childExitMethod));
		}

		//check for background child process completion
		if(bgCh){
			int exitMethod;
			for(i=0; i<bgCh; i++){
				int res = waitpid(bgChildren[i], &exitMethod, WNOHANG);
				//if a process has completed, print pid and exit status, remove pid from array
				if(res){
					printf("background pid %d is done: ", bgChildren[i]);
					if(WIFEXITED(exitMethod) != 0){
						printf("exit value %d\n", WEXITSTATUS(exitMethod));
					}
					else if(WIFSIGNALED(exitMethod) != 0){
						printf("terminated by signal %d\n", WTERMSIG(exitMethod));
					}
					else printf("Unexpected error in status\n");
					fflush(stdout);
					bgCh--;
					int j;
					for(j=i; j<bgCh; j++){
						bgChildren[j] = bgChildren[j+1];
					}
				}		
			}
		}

		printf(": "); //shell prompt
		//reset variables which need to be cleared at the start of each command
		argc=0;
		n=0;
		line=0;
		redirin=0;
		redirout=0;
		fflush(stdout);
		memset(command, '\0', CMD_LEN);
		memset(sourceFN, '\0', 120);
		memset(destFN, '\0', 120);

	//get command 
		//if command is longer than 2048 char, ignore it
		if(getline(&line, &n, stdin) < CMD_LEN){
			strcpy(command, line);
			free(line);
		}
		else{
			printf("Command entered exceeds maximum string length.\n");
			fflush(stdout);
			free(line);
			continue;
		}
		
		//ignore comment lines and blank lines
		if(!strcmp(command, "\n") || command[0] == '#')
			continue;

		//tokenize command into arguments
		char *token = strtok(command, " \n");
		int expandIdx = -1; //stores index of arg to expand

		while(token){
			argv[argc] = malloc(100 * sizeof(char));
			strcpy(argv[argc], token);
			if(strstr(token, "$$")){
				expandIdx = argc;
				char *cptr = strstr(argv[expandIdx], "$$");
				char *nextPart = cptr+2;
				*cptr = '\0';
				char *firstPart = argv[expandIdx];
				char newStr[CMD_LEN];
				sprintf(newStr, "%s%d%s", firstPart, getpid(), nextPart);
				strcpy(argv[expandIdx], newStr);
			}
			argc++;
			token = strtok(NULL, " \n");
		}
		argv[argc] = NULL; //last value set to NULL for execvp

		//built in exit command- ignore any arguments
		if(!strcmp(argv[0], "exit")){
			//kill off any background child processes
			for(i=0; i<bgCh; i++){
				kill(bgChildren[i], SIGTERM);
				waitpid(bgChildren[i], &childExitMethod, 0);
			}
			break;
		}

		//ignore lines with only space chars
		else if(argc == 0)
			continue;

		//if last arg is &, if in foreground only mode, strip it from command, otherwise run process in background
		if(!strcmp(argv[argc-1], "&")){
			if(!fgMode){
				//run process in background
				bg=1;
			}
			argv[argc-1] = NULL; //remove & from arguments
			argc--;
		}
	
		//check for redirection, store index of redir symbol and file name
		for(i=0; i<argc; i++){
			if(!strcmp(argv[i], "<")){
				redirin=i;
				strcpy(sourceFN, argv[i+1]);
				//remove < and file name from arguments (argv[redirin] and argv[redirin+1])
				//first remove <
				for(i=redirin; i<argc+1; i++){
					argv[i] = argv[i+1];
				}
				argc--;
				//now input file is at index redirin, remove that too
				for(i=redirin; i<argc+1; i++){
					argv[i] = argv[i+1];
				}
				argc--;
			}
		}
		//sets up redirection of stdin from /dev/null if no file specified
		if(bg && !redirin){
			strcpy(sourceFN, "/dev/null");
			redirin = 1;
		}
		for(i=0; i<argc; i++){
			if(!strcmp(argv[i], ">")){
				redirout=i;
				strcpy(destFN, argv[i+1]);
				//remove > and file name from arguments
				//first remove >
				for(i=redirout; i<argc+1; i++){
					argv[i] = argv[i+1];
				}
				argc--;
				//now output file is at index redirout, remove that too
				for(i=redirout; i<argc+1; i++){
					argv[i] = argv[i+1];
				}
				argc--;
			}
		}
		if(bg && !redirout){
			strcpy(destFN, "/dev/null");
			redirout = 1;
		}

		//built in cd command
		if(!strcmp(argv[0], "cd")){
			if(argc > 1){
				int success = chdir(argv[1]);
				if(success != 0){
					printf("Could not enter directory %s\n", argv[1]);
					fflush(stdout);
				}
				continue;
			}
			else if (argc == 1){
				int success = chdir(getenv("HOME"));
				if(success != 0){
					printf("Could not enter HOME directory %s\n", getenv("HOME"));
					fflush(stdout);
				}
				continue;
			}
		}

		//built in status command
		else if(!strcmp(argv[0], "status")){
			if(WIFEXITED(childExitMethod) != 0){
				printf("Process exited normally. Exit status %d\n", WEXITSTATUS(childExitMethod));
			}
			else if(WIFSIGNALED(childExitMethod) != 0){
				printf("Process terminated by signal %d\n", WTERMSIG(childExitMethod));
			}
			else printf("Unexpected error in status\n");
			fflush(stdout);
			continue;		
		}
		//all other commands pass off to exec
		else{
			childPID = fork();
			//error
			if(childPID == -1){
				printf("**** Fork error! ****");
				fflush(stdout);
				exit(1);
			}
			//this is the child process
			else if (childPID == 0){
				//set children to ignore SIGTSTP
				struct sigaction STP_ignore = {0};
				STP_ignore.sa_handler = SIG_IGN;
				STP_ignore.sa_flags = SA_RESTART;
				sigaction(SIGTSTP, &STP_ignore, NULL);
				//reset SIGINT to default for foreground processes
				if(!bg){
					struct sigaction SIGINT_act = {0}; //initialize empty struct for handling sigints
					SIGINT_act.sa_handler = SIG_DFL; //sets action to be function catchSIGINT
					sigfillset(&SIGINT_act.sa_mask); //delay all other signals during handler execution
					SIGINT_act.sa_flags = SA_RESTART;
					sigaction(SIGINT, &SIGINT_act, NULL); //register signal handler for SIGINT/ctrl+C
				}
				//redirect input if requested
				if(redirin){
					int inputFD = open(sourceFN, O_RDONLY);
					if(inputFD < 0){
						printf("Input file failed to open.\n");
						fflush(stdout);
						exit(1);
					}
					int result = dup2(inputFD, 0);
					fcntl(inputFD, F_SETFD, FD_CLOEXEC);
					if(result < 0){
						printf("Error redirecting stdin\n");
						fflush(stdout);
						exit(2);
					}
				}
				//redirect output if requested
				if(redirout){
					int outFD = open(destFN, O_WRONLY | O_CREAT | O_TRUNC, 0644);
					if(outFD < 0){
						printf("Output file failed to open.\n");
						fflush(stdout);
						exit(1);
					}
					int result = dup2(outFD, 1);
					fcntl(outFD, F_SETFD, FD_CLOEXEC);
					if(result < 0){
						printf("Error redirecting stdout\n");
						fflush(stdout);
						exit(2);
					}

				}				
				//pass off command to OS
				execvp(argv[0], argv);
				printf("Invalid command. execvp failed\n");
				fflush(stdout);
				exit(1); 
			}
			//this is the parent process
			//wait for foreground processes
			if(bg == 0){
				waitpid(childPID, &childExitMethod, 0);
			}
			else{
				bgChildren[bgCh] = childPID;
				bgCh++;
			}
			continue;
		}
	}

	return 0;
}
