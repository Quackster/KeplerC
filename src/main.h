#include <stdbool.h>

#ifndef MAIN_H
#define MAIN_H

#define COMMAND_INPUT_LENGTH 200

int main(void);
void exit_program();
void exit_signal_handler(int signal);
void dispose_program();

#endif
