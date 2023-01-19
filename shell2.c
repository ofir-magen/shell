#include <sys/stat.h>
#include <sys/wait.h>
#include <fcntl.h>
#include "stdio.h"
#include "errno.h"
#include "stdlib.h"
#include "unistd.h"
#include <string.h>
#include <signal.h>







#define QUIT "quit"

#define CONTROL_C "You typed Control-C!\n"
#define PIPE_STR "|"
#define EMPTY_STRING " "
#define EMPTY_CHAR ' '
#define AGAIN "!!"
#define STDOUT_CHR ">"
#define STDIN_CHR "<"
#define APPEND_STR ">>"
#define CD_STR "cd"
#define PROMPT_STR "prompt"
#define ECHO_STR "echo"
#define READ_STR "read"






int True = 1;
char *prompt;
int lastCommandStatus = -1;
char prevCommand[1024];
char tmpCommand[1024];
char command[1024];
int status = 0;
int number_of_pipes = 0;
int PIPE_WRITER = 1; 
int PIPE_READER = 0; 
int counter = 0;
int fildes[2];
char *argv[1024];
int stdoutfd;
int main_pid;
pid_t runningProcces = -1;

enum states
{
    NEUTRAL,
    WANT_THEN,
    THEN_BLOCK,
    ELSE_BLOCK
};
enum results
{
    SUCCESS,
    FAIL
};

int if_state = NEUTRAL;
int if_result = SUCCESS;
int last_stat = 0;

typedef struct Var
{
    char *key;
    char *value;
} Var;

typedef struct Node
{
  Var* data;
  struct Node *next;
} Node;

typedef struct List
{
  Node *head;
} List;

List variables;

// last_commands linked list for store all of recent commands
typedef struct last_commands {
    char command[1024];
    struct last_commands *next;
    struct last_commands *prev;
} last_commands;

last_commands *last_commands_root; 

void Add(List *list, Var *var){
 Node* new_node = (Node*) malloc(sizeof(Node));
    new_node->data = var;
    new_node->next = list->head;
    list->head = new_node;
}

char *astrcat(const char *a, const char *b)
{
    size_t na = strlen(a);
    size_t nb = strlen(b);
    char *c = malloc(na + nb + 1);
    if (!c)
        return NULL;
    memcpy(c, a, na);
    memcpy(c + na, b, nb + 1);
    return c;
}

char *Search_Var(char *key)
{
    Node *node = variables.head;
    while (node)
    {
        if (!strcmp(((Var *)node->data)->key, key))
        {
            return ((Var *)node->data)->value;
        }
        node = node->next;
    }
    return NULL;
}

void Handle_Read_Command(char *varName) {
    char input[1024];
    printf("%s: ", varName);
    char *dollar = "$";
    char *name = astrcat(dollar, varName);
    fgets(input, sizeof(input), stdin);
    input[strcspn(input, "\n")] = 0;
    // create a new variable with the input value
    Var *var = (Var*)malloc(sizeof(Var));
    var->key = (char*)malloc(strlen(name)+1);
    strcpy(var->key, name);
    var->value = (char*)malloc(strlen(input) + 1);
    strcpy(var->value, input);
    // add the variable to the list of variables
    Add(&variables, var);
}


void Change_Current_Dir(char *path)
{
    if (chdir(path) != 0)
    {
        printf("chdir() to %s failed\n", path);
        return;
    }
    printf("chdir() to %s\n", path);
}

void Ctrl_CHandler(int sig)
{
   
    strcpy(command, "^C");
    if (sig == SIGTSTP) {
        exit(0);
    }
    if (getpid() == main_pid) {
        printf(CONTROL_C);
        char message[2] = EMPTY_STRING;
        write(STDIN_FILENO, prompt, strlen(prompt)+1);
        write(STDIN_FILENO, message, strlen(message)+1);
    }
}

int handleRedirection(char **argv, char **outfile, int size)
{
    if (size >= 2 && (!strcmp(argv[size - 2], STDOUT_CHR) || !strcmp(argv[size - 2], APPEND_STR)))
    {
        *outfile = argv[size - 1];
        return STDOUT_FILENO;
    }
    else if (size >= 2 && !strcmp(argv[size - 2], "2>"))
    {
        *outfile = argv[size - 1];
        return STDERR_FILENO;
    }
    else if (size >= 2 && !strcmp(argv[size - 2], STDIN_CHR))
    {
        *outfile = argv[size - 1];
        return STDIN_FILENO;
    }

    return -1;
}

void Split_Command(char *command)
{
    char *token = strtok(command, EMPTY_STRING);
    int i = 0;

    while (token != NULL)
    {
        argv[i] = token;
        token = strtok(NULL, EMPTY_STRING);
        i++;
    }
    argv[i] = NULL;
}

char **findPipeCommand(char **args)
{
    char **p = args;
    while (*p != NULL)
    {
        if (strcmp(*p, PIPE_STR) == 0)
        {
            return p;
        }

        p++;
    }

    return NULL;
}

int argsCount(char **args)
{
    char **p = args;
    int cnt = 0;
    while (*p != NULL)
    {
        p++;
        cnt++;
    }

    return cnt;
}

int execute(char **args)
{
    char *outfile;
    int i = argsCount(args), fd, amper, redirect = -1, rv = -1;
    pid_t pid;
    int hasPip = 0;
    // find the first pipe sign.
   // returns pointer to the location of the character in the string,NULL otherwise.
    char **pipPointer = findPipeCommand(args); 
    int pipe_fd[2];

    // if there's a pipe use in the command
    if (pipPointer != NULL)
    {
        hasPip = 1;
        *pipPointer = NULL;
        i = argsCount(args);

        pipe(pipe_fd);

        if (fork() == 0)
        {
            close(pipe_fd[PIPE_WRITER]);
            close(STDIN_FILENO);
            dup(pipe_fd[PIPE_READER]);
            execute(pipPointer + 1);
            exit(0);
        }

        stdoutfd = dup(STDOUT_FILENO);
        dup2(pipe_fd[PIPE_WRITER], STDOUT_FILENO);
    }

    /* Is command empty */
    if (args[0] == NULL)
        return 0;

    if (strcmp(args[0], AGAIN)==0)
    {
        strcpy(tmpCommand, prevCommand);
        Split_Command(tmpCommand);
        execute(argv);
        return 0;
    }

    if (args[0][0] == '$' && i > 4)
    {
        Var *var = (Var *)malloc(sizeof(Var));
        var->key = malloc((strlen(args[0]) + 1));
        var->value = malloc((strlen(args[2]) + 1));

        strcpy(var->key, args[0]);
        strcpy(var->value, args[2]);

        Add(&variables, var);
        return 0;
    }

    if(strcmp(args[0], READ_STR)==0)
    {
        Handle_Read_Command(argv[1]);
        return 0;
    }

    if (strcmp(args[0], CD_STR)==0)
    {
        Change_Current_Dir(args[1]);
        return 0;
    }

    if (strcmp(args[0], PROMPT_STR)==0)
    {
        free(prompt);
        prompt = malloc(strlen(args[2]) + 1);
        strcpy(prompt, args[2]);
        return 0;
    }

    if (strcmp(args[0], ECHO_STR)==0)
    {
        char **ECHOargs = args + 1;
        if (strcmp(*ECHOargs, "$?")==0)
        {
            printf("%d\n", status);
            return 0;
        }

        while (*ECHOargs)
        {
            if (*ECHOargs && *ECHOargs[0] == '$')
            {
                char *v = Search_Var(*ECHOargs);
                if (v != NULL)
                    printf("%s ", v);
            }
        
            else
                printf("%s ", *ECHOargs);


            ECHOargs++;
        }
        printf("\n");
        return 0;
    }

    /* Does command line end with & */
    if (!strcmp(args[i - 1], "&"))
    {
        amper = 1;
        args[i - 1] = NULL;
    }
    else
        amper = 0;

    int redirectFd = handleRedirection(args, &outfile, i);

    /* for commands not part of the shell command language */

    if ((runningProcces = fork()) == 0)
    {
        /* redirection of IO ? */
        if (redirectFd >= 0)
        {
            if (!strcmp(args[i - 2], APPEND_STR))
            {
                fd = open(outfile, O_WRONLY | O_CREAT);
                lseek(fd, 0, SEEK_END);
            }
            else if (!strcmp(args[i - 2], STDOUT_CHR) || !strcmp(args[i - 2], "2>"))
            {
                fd = creat(outfile, 0660);
            }
            else
            {
                // stdin
                fd = open(outfile, O_RDONLY);
            }

            close(redirectFd);

            dup(fd);
            close(fd);
            /* stdout is now redirected */
            args[i - 2] = NULL;
        }

        // fprintf(stderr,"%s--\n" , args[0]);
        // printArgs(args);
        execvp(args[0], args);
    }
    /* parent continues here */
    if (amper == 0)
    {
        wait(&status);
        rv = status;
        runningProcces = -1;
    }

    if (hasPip)
    {
        close(STDOUT_FILENO);
        close(pipe_fd[PIPE_WRITER]);
        dup(stdoutfd);
        wait(NULL);
    }

    return rv;
}

int process(char **args);
// if date | grep Fri\n then\n echo "Shabat Shalom"\n else\n echo "Hard way to go" fi\n

int do_contol_command(char **args)
{
    char *cmd = argv[0];
    int rv = -1;

    if (strcmp(cmd, "if") == 0)
    {
        if (if_state != NEUTRAL)
        {
            printf("if unexpected");
            rv = 1;
        }
        else
        {
            last_stat = process(args + 1);
            if_result = (last_stat == 0) ? SUCCESS : FAIL;
            if_state = WANT_THEN;
            rv = 0;
        }
    }
    else if (strcmp(cmd, "then") == 0)
    {
        if (if_state != WANT_THEN)
        {
            printf("then unexpected");
            rv = 1;
        }
        else
        {
            if_state = THEN_BLOCK;
            rv = 0;
        }
    }
    else if (strcmp(cmd, "else") == 0)
    {
        if (if_state != THEN_BLOCK)
        {
            printf("else unexpected");
            rv = 1;
        }
        else
        {
            if_state = ELSE_BLOCK;
            rv = 0;
        }
    }
    else if (strcmp(cmd, "fi") == 0)
    {
        if (if_state != THEN_BLOCK && if_state != ELSE_BLOCK)
        {
            printf("fi unexpected");
            rv = 1;
        }
        else
        {
            if_state = NEUTRAL;
            rv = 0;
        }
    }

    return rv;
}

int is_control_command(char *s)
{
    return (strcmp(s, "if") == 0 || strcmp(s, "then") == 0 || strcmp(s, "else") == 0 || strcmp(s, "fi") == 0);
}

int is_ok_execute()
{
    int rv = 1;
    if (if_state == WANT_THEN)
    {
        rv = 0;
    }
    else if (if_state == THEN_BLOCK && if_result == SUCCESS)
    {
        rv = 1;
    }
    else if (if_state == THEN_BLOCK && if_result == FAIL)
    {
        rv = 0;
    }
    else if (if_state == ELSE_BLOCK && if_result == FAIL)
    {
        rv = 1;
    }
    else if (if_state == ELSE_BLOCK && if_result == SUCCESS)
    {
        rv = 0;
    }
    // printf("execute? %d %d\n",rv , if_result);
    return rv;
}

int process(char **args)
{
    int rv = -1;
    // do control command
    if (args[0] == NULL)
    {
        rv = 0;
    }
    else if (is_control_command(args[0]))
    {
        rv = do_contol_command(args);
    }
    else if (is_ok_execute())
    {
        // 2- execute
        rv = execute(args);
    }

    return rv;
}

// Handle with arrow up & down
void handle_arrows(char *token){
        // arrow up
    if(token != NULL && !strcmp(token,"\033[A")){ 
    
        if(last_commands_root->prev != NULL){ 
            strcpy(command, last_commands_root->prev->command);
            last_commands_root = last_commands_root->prev;
            token = strtok(command, EMPTY_STRING);
        }else{
            strcpy(command, last_commands_root->command);
        }
         // arrow down
    } else if(token != NULL && !strcmp(token,"\033[B")){

        if(last_commands_root->next != NULL){
            strcpy(command, last_commands_root->next->command);
            last_commands_root = last_commands_root->next;
            token = strtok(command, EMPTY_STRING);
        }else{
            strcpy(command, last_commands_root->command);
        }

    } else if (token != NULL && strlen(command) != 0){
        
        while(last_commands_root->next != NULL){
            last_commands_root = last_commands_root->next;
        }

        // Insert command to current last_commands
        strcpy(last_commands_root->command, prevCommand);

        last_commands *next = (last_commands*) malloc(sizeof(last_commands));
        next->prev = last_commands_root;
        last_commands_root->next = next;
        last_commands_root = last_commands_root->next;
        last_commands_root->next = NULL;
    }
}

int main()
{
    main_pid = getpid();
    signal(SIGINT, Ctrl_CHandler);
    // char command[1024];
    prompt = malloc(7);
    strcpy(prompt, "hello:");
    int commandPosition = -1;
    int i;
    char ch;
    char *token;
    char *b;

    //For store last commands
    last_commands_root = (last_commands*) malloc(sizeof(last_commands));
    last_commands_root->next = NULL;
    last_commands_root->prev = NULL;

    while (True)
    {
        printf("%s ", prompt);
       
        fgets(command , 1024, stdin);
        command[strlen(command) - 1] = '\0';

        // exit
        if (!strcmp(command, QUIT))
            break;

        // save last command
        if (strcmp(command, AGAIN))
            strcpy(prevCommand, command);

        token = strtok(prevCommand, EMPTY_STRING);

        handle_arrows(token);

        Split_Command(command);

        // handle command
        status = process(argv);
    }
}
