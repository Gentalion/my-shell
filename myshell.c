#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include "string.h"
#include <sys/stat.h>
#include <fcntl.h>
#include <signal.h>

typedef enum bound {
    NONE,
    WRITE_TO,
    READ_FROM,
    CONVEYOR,
    AND,
    OR
} bound_t;

struct command_struct {
    char** words;
    struct command_struct *next;
    bound_t bound;
    short background, open_bracket, close_bracket;
};

typedef struct command_struct * command;

char* string_from_file (FILE *f);
command init_command (char** words);
void free_command (command* cmd);
void free_single_command (command* cmd);
command split_string_into_commands (char *s);
void execute_command (command cmd);
void kill_zombie (int signum);

int main (int argc, char **argv) {
    signal (SIGINT, SIG_IGN);
    signal (SIGHUP, SIG_DFL);
    signal (SIGCHLD, kill_zombie);
    FILE* f;
    if (argc > 2) {
        fprintf (stderr, "Error: Omg, so many arguments! I don't know what to do. Try again.\n"); 

        return 0;
    }
    else if (argc == 1) {
        f = stdin;
    }
    else {
        f = fopen(argv[1], "r");
        if (f == NULL) {
            fprintf (stderr, "Error: Hmm, I don't know such file. Try again.\n");

            return 0;
        }
    }
    char *s;
    while ((s = string_from_file(f)) != NULL) {
        command first = split_string_into_commands (s);
        free (s);
        
        if ((first->words != NULL) && (first->words[0] != NULL))
            execute_command (first);
        
        free_command (&first);
    }

    fclose (f);
    return 0;
}

char* string_from_file (FILE *f) {
    const int default_string_size = 256;
    int c, string_size = 0, quotes_number = 0;
    char* string_buf = (char *) malloc (sizeof (char) * default_string_size);
    while (((c = fgetc(f)) != '\n') || ((quotes_number % 2) == 1)) {
        if (c == EOF) {
			if ((quotes_number % 2) == 1)
				fprintf (stderr, "Error: There are opened quotes left.");
			else if (string_size > 0)
				fprintf(stderr, "Error: There is no \\n in the end of string.");
            free (string_buf);
            return NULL;
        }

        if (c == '"')
            quotes_number++;

        string_buf[string_size] = c;
        string_size++;
        if (string_size % default_string_size == 0)
            string_buf = (char *) realloc (string_buf, sizeof(char) * (string_size + default_string_size));
    }

    string_buf[string_size] = '\0';
    return string_buf;
}

command init_command (char** words) {
    command a = (command) malloc (sizeof (struct command_struct));
    a->words = words;
    a->next = NULL;
    a->bound = NONE;
    a->background = 0;
    a->open_bracket = 0;
    a->close_bracket = 0;
    return a;
}

void free_command (command* cmd) {
    if (cmd == NULL)
        return;

    if (((*cmd)->next) != NULL)
        free_command (&((*cmd)->next));
    for (int i = 0; (*cmd)->words[i] != NULL; i++)
        free ((*cmd)->words[i]);
    free ((*cmd)->words);
    free (*cmd);
}

void free_single_command (command* cmd) {
    if (cmd == NULL)
        return;

    for (int i = 0; (*cmd)->words[i] != NULL; i++)
        free ((*cmd)->words[i]);
    free ((*cmd)->words);
    free (*cmd);
}

command split_string_into_commands (char *s) {
    const int default_size_of_string = 256, default_size_of_str_array = 100;
    int string_num = 0, char_num = 0, opened_quotes = 0, next_command = 0;
    long i = 0;
    command first = init_command (NULL), cur = first;
    cur->next = init_command (NULL);
    char** words = (char**) calloc (default_size_of_str_array , sizeof(char*));
    char* string_buf = (char*) calloc (default_size_of_string, sizeof(char));
    while (s[i] != '\0') {
        if (opened_quotes) {
            if (s[i] == '"')
                opened_quotes = 0;
            else {
                string_buf[char_num] = s[i];
                char_num++;
                if ((char_num % default_size_of_string) == 0)
                    string_buf = (char*) realloc (string_buf, sizeof(char) * (char_num + default_size_of_string));
            }
        }
        else switch (s[i])
        {
            case ' ':
                if (char_num) {
                    string_buf[char_num] = '\0';
                    words[string_num] = string_buf;
                    string_num++;
                    string_buf = (char*) calloc (default_size_of_string, sizeof(char));
                }
                char_num = 0;

                if (((string_num + 1) % default_size_of_str_array) == 0)
                    words = (char**) realloc (words, sizeof(char*) * (string_num + default_size_of_str_array));
                break;
            case '<':
                if (char_num) {
                    string_buf[char_num] = '\0';
                    words[string_num] = string_buf;
                    words[string_num + 1] = NULL;
                    string_buf = (char*) calloc (default_size_of_string, sizeof(char));
                    char_num = 0;
                }
                else 
                    words[string_num] = NULL;

                if (words[0] != NULL) {
                    cur->next->words = words;
                    words = (char**) calloc (default_size_of_str_array , sizeof(char*));
                    cur = cur->next;
                    cur->next = init_command (NULL);
                    cur->next->bound = READ_FROM;
                }
                string_num = 0;
                break;
            case '>':
                if (char_num) {
                    string_buf[char_num] = '\0';
                    words[string_num] = string_buf;
                    words[string_num + 1] = NULL;
                    string_buf = (char*) calloc (default_size_of_string, sizeof(char));
                    char_num = 0;
                }
                else 
                    words[string_num] = NULL;

                if (words[0] != NULL) {
                    cur->next->words = words;
                    words = (char**) calloc (default_size_of_str_array , sizeof(char*));
                    cur = cur->next;
                    cur->next = init_command (NULL);
                    cur->next->bound = WRITE_TO;
                }
                string_num = 0;
                break;
            case '|':
                if ((string_num == 0) && (char_num == 0) && (cur->next->bound == CONVEYOR))
                    cur->next->bound = OR;
                else {
                    if (char_num) {
                        string_buf[char_num] = '\0';
                        words[string_num] = string_buf;
                        words[string_num + 1] = NULL;
                        string_buf = (char*) calloc (default_size_of_string, sizeof(char));
                        char_num = 0;
                    }
                    else 
                        words[string_num] = NULL;

                    if (words[0] != NULL) {
                        cur->next->words = words;
                        words = (char**) calloc (default_size_of_str_array , sizeof(char*));
                        cur = cur->next;
                        cur->next = init_command (NULL);
                        cur->next->bound = CONVEYOR;
                    }
                    string_num = 0;
                }
                break;
            case '&':
                if (cur->next->background) {
                    cur->next->background = 0;
                    if (char_num) {
                        string_buf[char_num] = '\0';
                        words[string_num] = string_buf;
                        words[string_num + 1] = NULL;
                        string_buf = (char*) calloc (default_size_of_string, sizeof(char));
                        char_num = 0;
                    }
                    else 
                        words[string_num] = NULL;

                    if (words[0] != NULL) {
                        cur->next->words = words;
                        words = (char**) calloc (default_size_of_str_array , sizeof(char*));
                        cur = cur->next;
                        cur->next = init_command (NULL);
                        cur->next->bound = AND;
                    }
                    string_num = 0;
                }
                else
                    cur->next->background = 1;
                break;
            case '"':
                opened_quotes = !opened_quotes;
                break;
            case '(':
                if ((string_num != 0) || (char_num != 0)) {
                    fprintf(stderr, "Error: Order of brackets is violated.\n");
                    exit (1);
                }
                cur->next->open_bracket = cur->next->open_bracket + 1;
                break;
            case ')':
                if ((string_num == 0) && (char_num == 0)) { 
                    fprintf(stderr, "Error: Order of brackets is violated.\n");
                    exit (1);
                }
                cur->next->close_bracket = cur->next->close_bracket + 1;
                break;
            default:
                string_buf[char_num] = s[i];
                char_num++;
                if ((char_num % default_size_of_string) == 0)
                    string_buf = (char*) realloc (string_buf, sizeof(char) * (char_num + default_size_of_string));
        }
        if ((cur->next->background != 0) && (next_command != 0) && (s[i] != ' ') && (s[i] != ')')) {
            fprintf (stderr, "Error: Symbol & can be only in the end of a command.\n");
            exit (1);
        }
        else
            next_command = 0;
        if ((cur->next->background != 0) && (next_command == 0)) 
            next_command = 1;
        i++;
    }

	if (char_num) {
		string_buf[char_num] = '\0';
		words[string_num] = string_buf;
		words[string_num + 1] = NULL;
		string_num++;
	}
    else {
        free (string_buf);
        words[string_num] = NULL;
    }

	cur->next->words = words;
	if ((string_num == 0) && (first->next->words != words)) {
		if (cur->next->bound) {
			fprintf (stderr, "Error: Your input ends with >, < or |\n");
			exit (1);
		}
        free (words);
		free (cur->next);
	}

    command ret = first->next;
    if (ret->words[0] == NULL && ret->open_bracket != 0) {
        fprintf(stderr, "Error: Order of brackets is violated.\n");
        exit (1);
    }
    free (first);
    return ret;
}

void execute_command (command cmd) {
    int in = -1, out = -1, my_bracket = 0, current_bracket = 0, delta_bracket = 0, next_is_redirection = 0, end_status;
    int pipes[2];
    command trash;
    command cur = cmd;
    pid_t pid, sub = 1;
    for (; cur->next != NULL;) {
        for (; cur->open_bracket > 0; cur->open_bracket--) {
            current_bracket++;
            if ((sub = fork ()) > 0) {
                waitpid (sub, &end_status, 0);
                if (my_bracket > 0)
                    exit (end_status);
                current_bracket -= cur->close_bracket;
                while (current_bracket > my_bracket) {
                    current_bracket = current_bracket + cur->next->open_bracket - cur->next->close_bracket;
                    cur = cur->next;
                    if (cur->next == NULL && current_bracket != 0) {
                        fprintf(stderr, "Error: Order of brackets is violated.\n");
                        exit (1);
                    }   
                }
                cur = cur->next;
                if (cur == NULL)
                    break;
                if (current_bracket != my_bracket) {
                    fprintf(stderr, "Error: Order of brackets is violated.\n");
                    exit (1);
                }
            }
            else {
                my_bracket++;
                current_bracket -= cur->close_bracket;
            }
        }
        if (cur == NULL || cur->next == NULL)
            break;
        next_is_redirection = 0;
        if (my_bracket != 0)
            delta_bracket = cur->next->open_bracket - cur->next->close_bracket;
        else
            delta_bracket = 0;
        switch (cur->next->bound) {
            case READ_FROM:
                if (cur->next->words[1] != NULL) {
                    fprintf (stderr, "Error: There is more than one argument, where name of file is expected.\n");
                    exit (1);
                }
                if (in != -1) {
                    close (in);
                    fprintf (stderr, "Warning: Input file is redefined 2 or more times.\n");
                }
                in = open (cur->next->words[0], O_RDONLY, 0666);
                if (in == -1) {
                    fprintf (stderr, "Error: Unable to open file \"%s\"", cur->next->words[0]);
                    perror ("");
                    exit (1);
                }
                trash = cur->next;
                if (trash->background)
                    cur->background = trash->background;
                cur->next = cur->next->next;
                free_single_command (&trash);
                next_is_redirection = 1;
                break;
            case WRITE_TO:
                if (cur->next->words[1] != NULL) {
                    fprintf (stderr, "Error: There is more than one argument, where name of file is expected.\n");
                    exit (1);
                }
                if (out != -1) {
                    close (out);
                    fprintf (stderr, "Warning: Output file is redefined 2 or more times.\n");
                }
                out = open (cur->next->words[0], O_CREAT | O_WRONLY | O_TRUNC, 0666);
                if (out == -1) {
                    fprintf (stderr, "Error: Unable to open file \"%s\"", cur->next->words[0]);
                    perror ("");
                    exit (1);
                }
                trash = cur->next;
                cur->next = cur->next->next;
                if (trash->background)
                    cur->background = trash->background;
                free_single_command (&trash);
                next_is_redirection = 1;
                break;
            case CONVEYOR:
                if (pipe (pipes) == -1) {
                    perror ("Error: Unable to create a pipe");
                    exit (1);
                }
                if (out != -1)
                    fprintf (stderr, "Warning: Output file is redefined 2 or more times.\n");
                out = pipes[1];
                if ((pid = fork ()) == 0) {
                    close (pipes[0]);
                    dup2 (out, 1); // out = pipes[1] != -1
                    if (in != -1)
                        dup2 (in, 0);
                    execvp (cur->words[0], cur->words);
                    perror ("Error: Smth went wrong and exec didn't happen. Shutting up");
                    exit(1);
                }
                else {
                    close (pipes[1]);
                    if (in != -1)
                        close (in);
                    if (out != -1) {
                        close (out);
                        out = -1;
                    }
                    in = pipes[0];
                    cur = cur->next;
                }
                break;
            default:
                if ((cur->bound == NONE) || (cur->bound == CONVEYOR) || ((cur->bound == AND) && (end_status == 0)) || ((cur->bound == OR) && (end_status != 0))) {
                    if ((pid = fork ()) == 0) {
                        if (cur->background == 0)
                            signal (SIGINT, SIG_DFL);
                        if (out != -1)
                            dup2 (out, 1);
                        if (in != -1)
                            dup2 (in, 0);
                        execvp (cur->words[0], cur->words);
                        perror ("Error: Smth went wrong and exec didn't happen. Shutting up");
                        exit(1);
                    }   
                    else {
                        if (in != -1) {
                            close (in);
                            in = -1;
                        }
                        if (out != -1) {
                            close (out);
                            out = -1;
                        }
                        if (cur->background == 0)
                            waitpid (pid, &end_status, 0);
                        cur = cur->next;
                    }
                }
                else
                    cur = cur->next;
        }
        if (next_is_redirection)
            current_bracket = current_bracket + delta_bracket;
        if (current_bracket < my_bracket && my_bracket != 0)
            exit (end_status != 0);
        if (!next_is_redirection)
            current_bracket = current_bracket + delta_bracket;

    }
    if (cur != NULL) {
        if ((cur->bound == NONE) || (cur->bound == CONVEYOR) || ((cur->bound == AND) && (end_status == 0)) || ((cur->bound == OR) && (end_status != 0))) {
            if ((pid = fork ()) == 0) {
                if (cur->background == 0)
                    signal (SIGINT, SIG_DFL);
                if (out != -1)
                    dup2 (out, 1);
                if (in != -1)
                    dup2 (in, 0);
                execvp (cur->words[0], cur->words);
                perror ("Error: Smth went wrong and exec didn't happen. Shutting up");
                exit(1);
            }
            else {
                if (in != -1) {
                    close (in);
                    in = -1;
                }
                if (out != -1) {
                    close (out);
                    out = -1;
                }
                if (cur->background == 0)
                    waitpid (pid, &end_status, 0);
            }
        }
    }
    if (my_bracket != 0)
        exit (end_status != 0);
}

void kill_zombie (int signum) {
    wait (NULL);
}