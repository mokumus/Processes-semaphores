//
//  main.c
//  Midterm
//
//  Created by Muhammed Okumuş on 26.04.2021.
//  Copyright :copyright: 2021 Muhammed Okumuş. All rights reserved.
//

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>    // getopt(), ftruncate(), access(), read(), fork(), close(), _exit()
#include <errno.h>     // EINTR
#include <sys/wait.h>  // wait()
#include <semaphore.h> // sem_init(), sem_wait(), sem_post(), sem_destroy()
#include <fcntl.h>     // open(), O_CREAT, O_RDWR, O_RDONLY
#include <sys/mman.h>  // shm_open(), mmap(), PROT_READ, PROT_WRITE, MAP_SHARED, MAP_FAILED

int main(int argc, char *argv[])
{
    return 0;
}