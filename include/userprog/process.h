#ifndef USERPROG_PROCESS_H
#define USERPROG_PROCESS_H
#define VM

#include "threads/thread.h"
#include "vm/vm.h"

tid_t process_create_initd (const char *file_name);
tid_t process_fork (const char *name, struct intr_frame *if_);
int process_exec (void *f_name);
int process_wait (tid_t);
void process_exit (void);
void process_activate (struct thread *next);
void argument_stack(char **argv, int argc, struct intr_frame *if_); // if_는 인터럽트 스택 프레임 => 여기에다가 쌓는다.
struct thread *get_child_process ( int pid );
void remove_child_process (struct thread *cp);
bool install_page (void *upage, void *kpage, bool writable);
bool lazy_load_segment (struct page *page, void *aux);

/* 로드된 세그먼트의 정보를 담는 구조체 */
struct segment {
    struct file *file;          /* File to load from */
    off_t offset;               /* Offset within the file */
    uint32_t read_bytes;        /* Bytes to read from the file */
};

#endif /* userprog/process.h */
