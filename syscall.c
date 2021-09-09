#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "list.h"
#include "process.h"

static void syscall_handler (struct intr_frame *);
//my code begins
void* ptr_is_valid(const void*);
void exit(int status);

struct proc_file* list_search(struct list* files, int fd);

extern bool running;

struct proc_file
{
	struct file* ptr;
	int fd;
	struct list_elem elem;
};
//my code ends

void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

//my code begins
void* ptr_is_valid(const void *vaddr)
{
	if(!is_user_vaddr(vaddr))
	{
		exit(-1);
		return 0;
	}
	void *ptr=pagedir_get_page(thread_current()->pagedir,vaddr);
	if(!ptr)
	{
		exit(-1);
		return 0;
	}
	return ptr;
}
//my code ends


static void
syscall_handler (struct intr_frame *f UNUSED) 
{
  	int *p=f->esp;
	ptr_is_valid(p);
  	int system_call=*p;
	switch (system_call)
	{
		case SYS_EXIT:
			ptr_is_valid(p+1);
			exit(*(p+1));
			break;
		case SYS_EXEC:
			//printf("exec syscall.\n ");
			ptr_is_valid(p+1);
			ptr_is_valid(*(p+1));
			f->eax=use_exec(*(p+1));
			break;
		case SYS_WAIT:
			//printf("wait syscall.\n ");
			ptr_is_valid(p+1);
			f->eax=process_wait(*(p+1));
			break;
		case SYS_CREATE:
			//printf("create syscall.\n ");
			ptr_is_valid(p+5);
			ptr_is_valid(*(p+4));
			lock_filesystem();
			f->eax=filesys_create(*(p+4),*(p+5));
			unlock_filesystem();
			break;
		case SYS_REMOVE:
			ptr_is_valid(p+1);
			ptr_is_valid(*(p+1));
			lock_filesystem();
			if(filesys_remove(*(p+1))==NULL)
				f->eax=false;
			else
				f->eax=true;
			unlock_filesystem();
			break;
		case SYS_OPEN:
			ptr_is_valid(p+1);
			ptr_is_valid(*(p+1));

			lock_filesystem();
			struct file* fptr=filesys_open(*(p+1));
			unlock_filesystem();
			if(fptr==NULL)
				f->eax=-1;
			else
			{
				struct proc_file *pfile=malloc(sizeof(*pfile));
				pfile->ptr=fptr;
				pfile->fd=thread_current()->fd_count;
				thread_current()->fd_count++;
		list_push_back (&thread_current()->file_list,&pfile->elem);
				f->eax=pfile->fd;
			}
			break;
		case SYS_FILESIZE:
			ptr_is_valid(p+1);
			lock_filesystem();
f->eax=file_length(list_search(&thread_current()->file_list,*(p+1))->ptr);
			unlock_filesystem();
			break;
		case SYS_READ:
			ptr_is_valid(p+7);
			ptr_is_valid(*(p+6));
			if(*(p+5)==0)
			{
				int i;
				uint8_t* buffer=*(p+6);
				for(i=0;i<*(p+7);i++)
					buffer[i]=input_getc();
				f->eax=*(p+7);
			}
			else
			{
	struct proc_file* fptr=list_search(&thread_current()->file_list,*(p+5));
				if(fptr==NULL)
					f->eax=-1;
				else
				{
					lock_filesystem();
				f->eax=file_read(fptr->ptr,*(p+6),*(p+7));
					unlock_filesystem();
				}
			}
			break;

		case SYS_WRITE:
			//printf("write syscall.\n");
			ptr_is_valid(p+7);
			ptr_is_valid(*(p+6));
			//hex_dump(0,p,400,1);
			//putbuf in console.c
			if(*(p+5)==1)
			{
				putbuf(*(p+6),*(p+7));
				f->eax=*(p+7);
			}
			else
			{
struct proc_file* fptr=list_search(&thread_current()->file_list,*(p+5));
				if(fptr==NULL)
					f->eax=-1;
				else
				{
					lock_filesystem();
				f->eax=file_write(fptr->ptr,*(p+6),*(p+7));
					unlock_filesystem();
				}
			}
			break;
		case SYS_CLOSE:
			ptr_is_valid(p+1);
			lock_filesystem();
			close_file(&thread_current()->file_list,*(p+1));
			unlock_filesystem();
			break;
		case SYS_SEEK:
			ptr_is_valid(p+4);
			ptr_is_valid(p+5);
			lock_filesystem();
	file_seek(list_search(&thread_current()->file_list,*(p+4))->ptr,*(p+5));
			unlock_filesystem();
			break;
		default:
			printf("Default %d\n",*p);
	}
}

int use_exec(char *file_name)
{
	//printf("%d\n",thread_current()->tid);
	lock_filesystem();
	char* new_file=(char *)malloc(strlen(file_name)+1);
	strlcpy(new_file,file_name,strlen(file_name)+1);
	//save_ptr temporary only to use strtok_r
	char* save_ptr;
	new_file=strtok_r(new_file," ",&save_ptr);
	struct file* f=filesys_open(new_file);
	if(f==NULL)
	{
	  	unlock_filesystem();
	  	return -1;
	}
	{
	  	file_close(f);
	  	unlock_filesystem();
	  	return process_execute(file_name);
	}
}

void exit(int status)
{
	//printf("Exit : %s %d %d\n",thread_current()->name, thread_current()->tid, status);
	struct list_elem *itr;
      	for(itr=list_begin(&thread_current()->parent->child_proc);itr!=list_end (&thread_current()->parent->child_proc);itr=list_next(itr))
        {
          	struct child *f=list_entry(itr,struct child,elem);
          	if(f->tid==thread_current()->tid)
          	{
          		f->used=true;
          		f->exit_error=status;
          	}
        }
	thread_current()->exit_error=status;
	if(thread_current()->parent->waiting_on_thread==thread_current()->tid)
		sema_up(&thread_current()->parent->child_lock);
	thread_exit();
}

struct proc_file* list_search(struct list* files,int fd)
{
	struct list_elem *itr;
        for (itr=list_begin(files);itr!=list_end(files);itr=list_next(itr))
        {
          	struct proc_file *f=list_entry(itr,struct proc_file,elem);
          	if(f->fd==fd)
          		return f;
        }
   	return NULL;
}

void close_file(struct list* files, int fd)
{
	struct list_elem *itr;
	struct proc_file *f;
      	for(itr=list_begin(files);itr!=list_end(files);itr=list_next(itr))
        {
          	f=list_entry(itr,struct proc_file,elem);
          	if(f->fd==fd)
          	{
          		file_close(f->ptr);
          		list_remove(itr);
          	}
        }
    	free(f);
}

void close_all_files(struct list* files)
{
	while(!list_empty(files))
	{
		struct list_elem *itr=list_pop_front(files);
		struct proc_file *f=list_entry(itr,struct proc_file,elem);
	      	file_close(f->ptr);
	      	list_remove(itr);
	      	free(f);
	}
}
