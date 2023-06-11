#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H
#include "threads/synch.h"

void syscall_init (void);
//동기화를 위해 설정함
//이때 동기화란 한번에 하나의 프로세스만 실행되도록 동기화를 사용해야 한다는 것
struct lock filesys_lock;

#endif
