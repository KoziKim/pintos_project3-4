#ifndef USERPROG_PROCESS_H
#define USERPROG_PROCESS_H

#include "threads/thread.h"

tid_t process_create_initd (const char *file_name);
tid_t process_fork (const char *name, struct intr_frame *if_);
int process_exec (void *f_name);
int process_wait (tid_t);
void process_exit (void);
void process_activate (struct thread *next);
void argument_stack(char **argv, int argc, struct intr_frame *if_); // if_는 인터럽트 스택 프레임 => 여기에다가 쌓는다.
struct thread *get_child_process ( int pid );
void remove_child_process (struct thread *cp);

#endif /* userprog/process.h */
