#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/mman.h>
#include <stdbool.h>
#include <time.h>
#include <pwd.h>
#include <sys/types.h>
#include <sys/stat.h>

#define MAX_SIZE 1024

// 1. countPipe: count the number of the pipes in user input:
// input: the user input (string)
// output: the number of the pipes '|' (integer)
int countPipe(char* input){
    int count = 0;
    for (int i = 0; i < strlen(input); i++){
        if (input[i] == '|'){
            count++;
        }
    }
    return count;
}

// 2. tokenize: tokenize the string with specified delimiters, and store them into the array of string
// input: delimiter (char*), the user input (char*), and the empty 2d array argv (char**).
// output: none (void)
void tokenize(char* delimiter, char* input, char** argv){
    int index = 0;
    char *token = strtok(input, delimiter);
    while (token != NULL){
        argv[index] = token;
        token = strtok(NULL, delimiter); // NULL: tokenization with the same string
        index ++;
    }
    argv[index] = NULL; // NULL terminate with argument list
}

// 3. noPipeCommand: Shell commands with no pipe.
// input: the tokenized user input
// output: returns 0. exit(1) if forking fails
int noPipeCommand(char** argv){

    // 0. Shared memory for the changing directory
    // 0.1. Declare the shared memory
    char* shared_memory = mmap(NULL, MAX_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    // 0.2. First byte for the boolean value cd_called, checking if the directory is changed
    bool* cd_called = (bool*)shared_memory;
    // 0.4. Move the pointer by the size of boolean value.
    char* directory = shared_memory + sizeof(bool); 
    // 0.5. initialize the boolean value cd_called
    *cd_called = false;

    // Command for exit
    if(strcmp(argv[0],"exit")==0){
        exit(0);
    }
    pid_t child = fork();

    // 1. Check if forking succeeds
    if (child < 0){
        perror("fork failed.");
        exit(1);
    }
    // 2. Parent process: wait and check the directory
    else if(child > 0){
        // 2.1 wait until the child process is done.
        wait(NULL);
        // 2.2 change the directory when cd is called. 
        if (*cd_called) {
            if (chdir(directory) == -1) {
                perror("cd: error changing directory in parent");
            }
        }
    }
    // 3. Child process: execute the command
    else{
        // 3.1 cd
        if (strcmp(argv[0],"cd")==0){
            if (argv[1] == NULL) {
                printf("cd: directory name required.\n");
            } 
            else {
                // copy the directory name to the shared memory
                strcpy(directory, argv[1]);
                // switch the boolean value as true.
                *cd_called = true;
            }
        }
        // 3.2 pwd
        else if (strcmp(argv[0],"pwd")==0){
            char cwd[MAX_SIZE];
            if (getcwd(cwd, sizeof(cwd)) != NULL) {
                printf("%s\n", cwd);
            } 
            else {
                perror("pwd: failed to retrieve the current directory.");
            }
        }
        // 3.3. ls
        else if(strcmp(argv[0],"ls")==0){
            if(execvp("ls", argv)<0){
                printf("ls: command error.\n");
            }
        }
        // 3.4 mkdir
        else if(strcmp(argv[0],"mkdir")==0){
            if(argv[1]==NULL){
                printf("mkdir: directory name...\n");
            }
            else{
                if(mkdir(argv[1],0777) == -1){
                    perror("mkdir: failed to make the new directory");
                }
                else{
                    printf("mkdir: directory %s is made\n",argv[1]);
                }
            }
        }
        // 3.5 clear
        else if (strcmp(argv[0],"clear")==0){
            system("clear");
        }
        // 3.6 cat
        else if (strcmp(argv[0], "cat") == 0) {

            if (argv[1] == NULL) {
                printf("cat: file name...\n");
                return 0;
            }

            FILE *file;
            char buffer[MAX_SIZE];

            file = fopen(argv[1], "r");
            if (!file) {
                perror("cat: cannot open the file");
                exit(0);
            }

            while (fgets(buffer, MAX_SIZE, file) != NULL) {
                printf("%s", buffer); 
            }

            fclose(file);
        }

        // 3.7. rm
        else if(strcmp("rm",argv[0])==0){
            if (argv[1] == NULL) {
                printf("rm: filename...\n");
            }
            else{
                if (remove(argv[1]) == 0) {
                    printf("rm: File %s is removed\n",argv[1]);
                }
                else{
                    perror("rm: File removal failed");
                }
            }
        }
        // 3.8. date
        else if (strcmp("date", argv[0]) == 0) {
            time_t t = time(NULL);
            time(&t);
            printf("date: %s", ctime(&t));
        }

        // 3.9. execvp
        else {
            if(execvp(argv[0], argv)<0){
                printf("%s: command not found.\n",argv[0]);
            }
        }
    }

    // clear the memory
    munmap(shared_memory, MAX_SIZE);
    return 0;
}

// 4. onePipeCommand: Shell commands with one pipe
// input: user_input1, user_input2
// output: returns 0.
// Procedure: Parent -> child1 -> child2(first command) -> child1(second command) -> parent
int onePipeCommand(char** argv1, char** argv2){
    
    // 1. Create the pipe and check for errors.
    int pipefds[2];
    if(pipe(pipefds) == -1){
        perror("pipe failed");
        exit(1);
    }

    // 2. Fork child1 and check for errors.
    pid_t child1 = fork();
    if (child1 < 0){
        perror("child1 fork failed.");
        exit(1);
    }

    // 3. In child1
    else if(child1 == 0){

        // 3.1. Fork child2 and check for errors.
        pid_t child2 = fork();
        if (child2 < 0){
            perror("child2 fork failed.");
            exit(1);
        }

        // 3.2. In child2, execute the first command through pipe
        else if(child2 == 0){
            // redirect the STDOUT to the write end of the pipe
            dup2(pipefds[1], STDOUT_FILENO);
            // close rhe read end. Prevent the potential deadlock
            close(pipefds[0]);
            // close the write end as well. 
            // After the dup2 call, the standard output of the process is already redirected to the write end of the pipe.
            // The original file descriptor pipefds[1] is now redundant, because we have two file descriptors pointing to the same "place" (the write end of the pipe). 
            // By closing pipefds[1], we ensure that there's only one file descriptor (the standard output) that can write to the pipe. 
            close(pipefds[1]);
            if (execvp(argv1[0], argv1) < 0){
                perror("First command failed to execute");
                exit(1);
            }
          
        }
        // 3.3. In child1, execute the second command
        else{
            // redirect STD_IN to the read end.
            dup2(pipefds[0], STDIN_FILENO);
            // close the write end that is not in use
            close(pipefds[1]);
            // close the read end as well.
            close(pipefds[0]);
            if (execvp(argv2[0], argv2) < 0){
                perror("Second command failed to execute");
                exit(1);
            }

        }
    }
    // 4. In the parent, wait for child1 to finish
    else{
        // make sure the both ends of the pipes are closed
        close(pipefds[0]);
        close(pipefds[1]);
        wait(NULL);
        wait(NULL);  // Wait for both child processes
    }
    return 0;
}

// 4. twoPipeCommand: Shell commands with two pipes
// input: user_input1, user_input2, user_input3
// output: returns 0.
// Procedure: Parent -> child1 -> child2 ->child3(first command) -> child2(second command) -> child1(third command) -> parent
int twoPipeCommand(char** argv1, char** argv2, char** argv3){

    // Make two pipes
    // pipefds1 for child3 and child2, pipefds2 for child2 and child1.
    int pipefds1[2];
    int pipefds2[2];
    if(pipe(pipefds1) == -1){
        perror("pipe1 failed");
        exit(1);
    }    
    if(pipe(pipefds2) == -1){
        perror("pipe2 failed");
        exit(1);
    }
    // Fork child1 and check the error.
    pid_t child1 = fork();
    if (child1 < 0){
        perror("child1 fork failed.");
        exit(1);
    }
    // In child1
    else if(child1 == 0){
        // Fork child2 and check the error.
        pid_t child2 = fork();
        if (child2 < 0){
            perror("child2 fork failed.");
            exit(1);
        }
        
        // In child2
        else if(child2 == 0){
            // Fork child3 and check the error.
            pid_t child3 = fork();
            if (child3 < 0){
                perror("child3 fork failed."); 
                exit(1);
            }
            // In child3
            else if (child3==0){
                // Redirect STDOUT of the first command to the write end of pipefds1
                dup2(pipefds1[1],STDOUT_FILENO);
                close(pipefds1[0]);
                close(pipefds1[1]);
                // Execute the first command
                if (execvp(argv1[0],argv1) < 0){
                    perror("First command failed to execute");
                    exit(1);
                }
                exit(0);
            }
            // In child 2
            else{
                // Wait for child3 to finish
                wait(NULL);  
                // Redirect STDIN of the first command to the read end of pipefds1
                dup2(pipefds1[0],STDIN_FILENO);
                // Redirect STDOUT of the second command to the write end of pipefds2
                dup2(pipefds2[1],STDOUT_FILENO);
                close(pipefds1[1]);
                close(pipefds1[0]);
                close(pipefds2[1]);
                close(pipefds2[0]);

                // Execute the second command
                if (execvp(argv2[0],argv2) < 0){
                    perror("Second command failed to execute");
                    exit(1);
                }
                exit(0);
            }
        }
        // child 1
        else{
            // Wait for child2 to finish
            wait(NULL);  
            // Redirect STDIN of the second command to the read end of pipefds2
            dup2(pipefds2[0],STDIN_FILENO);
            close(pipefds2[1]);
            close(pipefds2[0]);
            if (execvp(argv3[0], argv3) < 0) {
                perror("Third command failed to execute");
                exit(1);
            }
            exit(0);
        }
    }
    else{
        wait(NULL);  // Wait for child1 to finish
        close(pipefds1[1]);
        close(pipefds1[0]);
        close(pipefds2[1]);
        close(pipefds2[0]);
        return 0;
    }
}


// 4. threePipeCommand: Shell commands with three pipes
// input: user_input1, user_input2, user_input3, user_input4
// output: returns 0.
// Procedure: Parent -> child1 -> child2 ->child3 -> child4(first command)-> child3 (second command)-> child2(third command) -> child1(fourth command) -> parent
int threePipeCommand(char** argv1, char** argv2, char** argv3, char** argv4){

    // Make two pipes
    // pipefds1 for child4 and child3, pipefds2 for child3 and child2, pipefds3 for child2 and child1.
    int pipefds1[2];
    int pipefds2[2];
    int pipefds3[2];
    if(pipe(pipefds1) == -1){
        perror("pipe1 failed");
        exit(1);
    }    
    if(pipe(pipefds2) == -1){
        perror("pipe2 failed");
        exit(1);
    }
    if(pipe(pipefds3) == -1){
        perror("pipe3 failed");
        exit(1);
    }
    // Fork child1 and check the error.
    pid_t child1 = fork();
    if (child1 < 0){
        perror("child1 fork failed.");
        exit(1);
    }

    // In child1
    else if(child1 == 0){
        // Fork child2 and check the error.
        pid_t child2 = fork();
        if (child2 < 0){
            perror("child2 fork failed.");
            exit(1);
        }
        // In child2
        else if (child2==0){
            // Fork child3 and check the error.
            pid_t child3 = fork();
            if (child3 < 0){
                perror("child3 fork failed.");
                exit(1);
            }
            // In child3
            else if(child3==0){
                // Fork child4 and check the error.
                pid_t child4 = fork();
                if (child4 < 0){
                    perror("child4 fork failed.");
                    exit(1);
                }
                // In child4
                else if(child4==0){
                    // Redirect STDOUT of the first command to the write end of pipefds1
                    dup2(pipefds1[1],STDOUT_FILENO);
                    close(pipefds1[0]);
                    close(pipefds1[1]);
                    // Execute the first command
                    if (execvp(argv1[0],argv1) < 0){
                        perror("First command failed to execute");
                        exit(1);
                    }
                    exit(0);
                }
                // In child3
                else{
                    // Wait for child4 to finish
                    wait(NULL);  
                    // Redirect STDIN of the first command to the read end of pipefds1
                    dup2(pipefds1[0],STDIN_FILENO);
                    // Redirect STDOUT of the second command to the write end of pipefds2
                    dup2(pipefds2[1],STDOUT_FILENO);
                    close(pipefds1[1]);
                    close(pipefds1[0]);
                    close(pipefds2[1]);
                    close(pipefds2[0]);

                    // Execute the second command
                    if (execvp(argv2[0],argv2) < 0){
                        perror("Second command failed to execute");
                        exit(1);
                    }
                    exit(0);
                }
            }
            // In child2
            else{
                // Wait for child3 to finish
                wait(NULL);  
                // Redirect STDIN of the second command to the read end of pipefds3
                dup2(pipefds2[0],STDIN_FILENO);
                // Redirect STDOUT of the third command to the write end of pipefds3
                dup2(pipefds3[1],STDOUT_FILENO);
                close(pipefds2[1]);
                close(pipefds2[0]);
                close(pipefds3[1]);
                close(pipefds3[0]);

                // Execute the second command
                if (execvp(argv3[0],argv3) < 0){
                    perror("Third command failed to execute");
                    exit(1);
                }
                exit(0);
            }

        }
        // in child1
        else{
            // wait for child2 to finish
            wait(NULL);
            // Redirect STDIN of the second command to the read end of pipefds2
            dup2(pipefds3[0],STDIN_FILENO);
            close(pipefds3[1]);
            close(pipefds3[0]);
            if (execvp(argv4[0],argv4) < 0){
                perror("Fourth command failed to execute");
                exit(1);
            }
                exit(0);
        }
    }
    // parent
    else{
        wait(NULL);
        close(pipefds1[1]);
        close(pipefds1[0]);
        close(pipefds2[1]);
        close(pipefds2[0]);
        close(pipefds3[1]);
        close(pipefds3[0]);
        return 0;
    }
}

int main(){

    char input[MAX_SIZE];
    while(1){
        printf("$> ");
        // Read user input and store it into "input"
        fgets(input, MAX_SIZE, stdin);
        // Remove trailing newline character
        size_t len = strlen(input);
        if (len > 0 && input[len - 1] == '\n') {
            input[len - 1] = '\0';
        }

        // Count the number of pipes
        int pipe_num = countPipe(input);

        // Check if there are more than 3 pipes
        if (pipe_num > 3){
            printf("There should be at most 3 pipes for the command\n");
        }
        // Case 1: When there is no pipe
        else if(pipe_num == 0){
            char* argv[MAX_SIZE];
            tokenize("\t\n ",input,argv);
            noPipeCommand(argv);
        }
        // Case 2: When there is 1 pipe
        else if(pipe_num==1){
            char* argv[MAX_SIZE];
            tokenize("|",input,argv);
            char *argv1[MAX_SIZE];
            tokenize("\t\n ",argv[0],argv1);
            char *argv2[MAX_SIZE]; 
            tokenize("\t\n ",argv[1],argv2);
            onePipeCommand(argv1,argv2);
        }
        // Case 3: When there are 2 pipes
        else if(pipe_num==2){
            char* argv[MAX_SIZE];
            tokenize("|",input,argv);
            char *argv1[MAX_SIZE];
            tokenize("\t\n ",argv[0],argv1);
            char *argv2[MAX_SIZE]; 
            tokenize("\t\n ",argv[1],argv2);
            char *argv3[MAX_SIZE]; 
            tokenize("\t\n ",argv[2],argv3);
            twoPipeCommand(argv1,argv2,argv3);
        }
        // Case 4: When there are 3 pipes
        else{
            char* argv[MAX_SIZE];
            tokenize("|",input,argv);
            char *argv1[MAX_SIZE];
            tokenize("\t\n ",argv[0],argv1);
            char *argv2[MAX_SIZE]; 
            tokenize("\t\n ",argv[1],argv2);
            char *argv3[MAX_SIZE]; 
            tokenize("\t\n ",argv[2],argv3);
            char *argv4[MAX_SIZE]; 
            tokenize("\t\n ",argv[3],argv4);
            threePipeCommand(argv1,argv2,argv3,argv4);
        }
    }

    return 0;
}