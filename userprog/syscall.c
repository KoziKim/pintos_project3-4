#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/loader.h"
#include "userprog/gdt.h"
#include "threads/flags.h"
#include "intrinsic.h"
#include "threads/synch.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "threads/palloc.h"

typedef int pid_t;
#define PID_ERROR ((pid_t) -1)

struct lock filesys_lock;

void syscall_entry (void);
void syscall_handler (struct intr_frame *);

/* System call.
 *
 * Previously system call services was handled by the interrupt handler
 * (e.g. int 0x80 in linux). However, in x86-64, the manufacturer supplies
 * efficient path for requesting the system call, the `syscall` instruction.
 *
 * The syscall instruction works by reading the values from the the Model
 * Specific Register (MSR). For the details, see the manual. */

#define MSR_STAR 0xc0000081         /* Segment selector msr */
#define MSR_LSTAR 0xc0000082        /* Long mode SYSCALL target */
#define MSR_SYSCALL_MASK 0xc0000084 /* Mask for the eflags */

void halt (void);
void exit (int status);
pid_t fork(struct intr_frame *f);
int exec (char *file_name);
int wait (pid_t pid);
bool create (const char *file, unsigned initial_size);
bool remove (const char *file);
int open (const char *file);
int filesize (int fd);
int read (int fd, void *buffer, unsigned size);
int write (int fd, const void *buffer, unsigned size);
void seek(int fd, unsigned position);
unsigned tell(int fd);
void close(int fd);

void
syscall_init (void) {
	lock_init(&filesys_lock);

	write_msr(MSR_STAR, ((uint64_t)SEL_UCSEG - 0x10) << 48  |
			((uint64_t)SEL_KCSEG) << 32);
	write_msr(MSR_LSTAR, (uint64_t) syscall_entry);

	/* The interrupt service rountine should not serve any interrupts
	 * until the syscall_entry swaps the userland stack to the kernel
	 * mode stack. Therefore, we masked the FLAG_FL. */
	write_msr(MSR_SYSCALL_MASK,
			FLAG_IF | FLAG_TF | FLAG_DF | FLAG_IOPL | FLAG_AC | FLAG_NT);
}

/* The main system call interface */
void
syscall_handler (struct intr_frame *f ) {
	// TODO: Your implementation goes here.
	// uintptr_t stack_pointer = f->rsp;
	// check_address(stack_pointer);
	switch(f->R.rax) {
		case SYS_HALT:
			halt();
			break;
		case SYS_EXIT:
			exit(f->R.rdi);
			break;
		case SYS_FORK:
			f->R.rax = fork(f);
			break;
		case SYS_EXEC:
			f->R.rax = exec(f->R.rdi);
			break;
		case SYS_WAIT:
			f->R.rax = wait(f->R.rdi);
			break;
		case SYS_CREATE:
			f->R.rax = create(f->R.rdi, f->R.rsi);
			break;
		case SYS_REMOVE:
			f->R.rax = remove(f->R.rdi);
			break;
		case SYS_OPEN:
			f->R.rax = open(f->R.rdi);
			break;
		case SYS_FILESIZE:
			f->R.rax = filesize(f->R.rdi);
			break;
		case SYS_READ:
			f->R.rax = read(f->R.rdi, f->R.rsi, f->R.rdx);
			break;
		case SYS_WRITE:
			f->R.rax = write(f->R.rdi, f->R.rsi, f->R.rdx);
			break;
		case SYS_SEEK:
			seek(f->R.rdi, f->R.rsi);
			break;
		case SYS_TELL:
			f->R.rax = tell(f->R.rdi);
			break;
		case SYS_CLOSE:
			close(f->R.rdi);
			break;
		default:
			printf ("system call!\n");
			thread_exit ();		
	}
	// printf ("system call!\n");
	// thread_exit ();
}

void
check_address (void *addr) {
	if (is_kernel_vaddr(addr)) {
		exit(-1);
	}
}

void halt(void) {
	power_off();
}

void exit(int status) {
	struct thread *curr = thread_current();
	curr->exit_status = status;
	printf("%s: exit(%d)\n", thread_name(), status); 
	thread_exit();
}

pid_t fork(struct intr_frame *f) {
	const char *thread_name= f->R.rdi;
	pid_t tid = process_fork(thread_name, f);

	return tid;
}

int exec (char *file_name) {
	check_address(file_name);
	int file_size = strlen(file_name)+1;
	char *fn_copy = palloc_get_page(PAL_ZERO);
	if (fn_copy==NULL) {
		exit(-1);
	}
	strlcpy(fn_copy, file_name, file_size);
	if (process_exec(fn_copy)==-1) {
		return -1;
	}
	NOT_REACHED();
	return 0;
} 

int wait (pid_t pid) {
	return process_wait(pid);
}

bool create (const char *file, unsigned initial_size)
{
	if (file == NULL) {
		exit(-1);
	}
	// check_address(file);
	return filesys_create(file, initial_size);
}

bool remove (const char *file)
{	
	if (file == NULL) {
		exit(-1);
	}
	// check_address(file);
	return filesys_remove(file);
}

int add_file_to_fdt (struct file *file);
static struct file * find_file_by_fd (int fd);
void remove_file_from_fdt (int fd);

int open (const char *file)
{
	check_address(file);
	if (strcmp(file, "") == 0 || file == NULL) {
		return -1;
	}
	struct file *fileobj = filesys_open(file);
	if (fileobj == NULL){
		return -1;
	}

	int fd = add_file_to_fdt(fileobj);
	return fd;
}

int filesize (int fd) {
	struct file *open_file = find_file_by_fd(fd);
	if (open_file == NULL) {
		return -1;
	}
	return file_length(open_file);
}

int read (int fd, void *buffer, unsigned size) {
	check_address(buffer);
	lock_acquire(&filesys_lock);
	off_t read_size = 0;
	char *read_buffer = (char *)buffer;

	/* STDIN */
	if (fd == 0) { // 표준 입력일 때. 키보드 입력만 받음.
		while (read_size < size) {
			read_buffer[read_size] = input_getc();
			if (read_buffer[read_size] == '\n'){
				break;
			}
			read_size++;
		}
		read_buffer[read_size]='\0';
		lock_release(&filesys_lock);
		return read_size;
	}
	else { // 표준 입력이 아닐 때. 즉, 파일의 데이터를 읽어온다.
		struct file *file_ptr = find_file_by_fd(fd);
		if (file_ptr == NULL){
			lock_release(&filesys_lock);
			return -1;
		}
		read_size = file_read(file_ptr, read_buffer, size);
		lock_release(&filesys_lock);
		return read_size;
	}
}

int write (int fd, const void *buffer, unsigned size) {
	check_address(buffer);
	lock_acquire(&filesys_lock);
	off_t written_size = 0;
	char *write_buffer = (char *)buffer;

	/* STDOUT */
	if (fd == 1) { // 표준 출력일 때. 버퍼에 쌓여있는 데이터(문자열)을 화면에 출력함.
		putbuf(write_buffer, size);
		lock_release(&filesys_lock);
		return size;
	}
	else { // 표준 출력이 아닐 때. 버퍼에 쌓여있는 데이터(문자열)를 파일에 기록한다.
		struct file *file_ptr = find_file_by_fd(fd);
		// 아래의 예외처리는 틀렸다. write()의 경우 file_ptr가 NULL인 것이 논리적으로 다분히 가능하기 때문이다.
		// if (file_ptr == NULL){
		// 	lock_release(&filesys_lock);
		// 	return -1;
		// }
		written_size = file_write(file_ptr, write_buffer, size);
		lock_release(&filesys_lock);
		return written_size;
	}
}

void seek(int fd, unsigned position) {
	if (fd < 2 || fd > FDCOUNT_LIMIT){
		return;
	}
	struct file *file_ptr = find_file_by_fd(fd);
	
	if (file_ptr == NULL){
		return;
	}
	file_seek(file_ptr, position);
}

unsigned tell(int fd) {
	if (fd < 2 || fd > FDCOUNT_LIMIT){
		return;
	}
	struct file *file_ptr = find_file_by_fd(fd);

	return file_tell(file_ptr);
}

void close(int fd)
{
	// file 주소를 fd 와 find_file_by_fd()로 찾는다.
	struct file *fileobj = find_file_by_fd(fd);
	if (fd < 0 || fd >= FDCOUNT_LIMIT || fileobj == NULL) {
		return;
	}

	remove_file_from_fdt(fd);

	file_close(fileobj);
}

int add_file_to_fdt (struct file *file) {
	struct thread *curr  = thread_current();
	struct file **fdt = curr->fdt;
	while (curr->next_fd < FDCOUNT_LIMIT && fdt[curr->next_fd]) {
		curr->next_fd++;
	}
	if (curr->next_fd >= FDCOUNT_LIMIT) 
		return -1;
	fdt[curr->next_fd] = file;
	return curr->next_fd;
}

static struct file * find_file_by_fd (int fd) {
	struct thread *curr = thread_current();
	if (fd < 0 || fd >= FDCOUNT_LIMIT) {
		return NULL;
	}
	return curr->fdt[fd];
}

void remove_file_from_fdt (int fd) {
	struct thread *curr = thread_current();
	if (fd < 0 || fd >= FDCOUNT_LIMIT) {
		return NULL;
	}
	curr->fdt[fd] = NULL;
}
