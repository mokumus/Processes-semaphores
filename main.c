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
#include <stdarg.h>
#include <string.h>

#define DEBUG 0
#define SHARED_LINK "tmp_shared"

//Macros
#define errExit(msg)        \
    do                      \
    {                       \
        perror(msg);        \
        exit(EXIT_FAILURE); \
    } while (0)

// Globals
int _N = 0, opt_N = 0, // # of nurses
    _V = 0, opt_V = 0, // # of vaccinators
    _C = 0, opt_C = 0, // # of citizens
    _B = 0, opt_B = 0, // tc+1 size of the buffer
    _T = 0, opt_T = 0, // # of times each citizen must receive the 2 shots
    shm_fd;            // shared memory file descriptor

char _I[255]; // file path for the supplier input
int opt_I;

pid_t *pid;

sig_atomic_t exit_requested = 0;

// Shared data
struct ClinicData
{
    sem_t sem_shm_access;
    sem_t sem_full;
    sem_t sem_empty;
    sem_t sem_pair;
    sem_t sem_stored;

    int pfizer;
    int sputnik;

    int clinic_empty_slots;
    int n_vaccinated;
    int n_needs_vaccine;

    int file_index;

    int nurses_done;
    int total_carried;
};

// Function Prototypes
void print_usage(void);
void debug_printf(const char *format, ...);
int validate_inputs(void);
void print_inputs(void);
void sig_handler(int sig_no);

void nurse(char *input_file, struct ClinicData *data, int id);
void vaccinator(struct ClinicData *data, int id);
void citizen(struct ClinicData *data, int id);

int s_wait(sem_t *sem, char *msg);
int s_post(sem_t *sem, char *msg);
int s_init(sem_t *sem, int val, char *msg);

int main(int argc, char *argv[])
{
    int option;
    pid_t parent_pid = getpid();

    // Input parsing & validation =======================
    while ((option = getopt(argc, argv, "n:v:c:b:t:i:")) != -1)
    { //get option from the getopt() method
        switch (option)
        {
        case 'n':
            opt_N = 1;
            _N = atoi(optarg);
            break;
        case 'v':
            opt_V = 1;
            _V = atoi(optarg);
            break;
        case 'c':
            opt_C = 1;
            _C = atoi(optarg);
            break;
        case 'b':
            opt_B = 1;
            _B = atoi(optarg);
            break;
        case 't':
            opt_T = 1;
            _T = atoi(optarg);
            break;
        case 'i':
            opt_I = 1;
            snprintf(_I, 255, "%s", optarg);
            break;
        default:
            print_usage();
            exit(EXIT_FAILURE);
        }
    }

    for (; optind < argc; optind++) //when some extra arguments are passed
        debug_printf("Given extra arguments: %s\n", argv[optind]);

    if (!validate_inputs())
    {
        debug_printf("Missing parameters. Please read the usage information...\n");
        print_usage();
        exit(EXIT_FAILURE);
    }

    print_inputs();

    setbuf(stdout, NULL); // Disable stdout buffering for library functions
    // Register handlers before forking
    signal(SIGINT, sig_handler);

    //Create shared memory and initlize it========================================================
    shm_unlink(SHARED_LINK);
    shm_fd = shm_open(SHARED_LINK, O_CREAT | O_RDWR, S_IRUSR | S_IWUSR);

    if (shm_fd == -1)
        errExit("shm_open @main");
    if (ftruncate(shm_fd, sizeof(struct ClinicData)) == -1)
        errExit("ftruncate @main");

    struct ClinicData *clinic = mmap(NULL, sizeof(struct ClinicData), PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);

    if (clinic == MAP_FAILED)
        errExit("mmap @main");

    clinic->n_needs_vaccine = 40;
    clinic->clinic_empty_slots = 30;
    clinic->n_vaccinated = 0;

    printf("Welcome to the GTU344 clinic. Number of citizen to vaccinate c=%d\n", _C);
    /* Initialize semaphores */

    s_init(&clinic->sem_shm_access, 1, "sem_init @main - sem_shm_access");
    s_init(&clinic->sem_full, 0, "sem_init @main - sem_full");
    s_init(&clinic->sem_empty, _B, "sem_init @main - sem_empty");
    s_init(&clinic->sem_pair, 0, "sem_init @main - sem_pair");
    s_init(&clinic->sem_stored, 0, "sem_init @main - sem_stored");

    //=======================================================Create shared memory and initlize it

    // Create actor process=========================================
    pid = malloc((_N + _V + _C + 1) * sizeof(pid_t));
    for (int i = 0; i < _N + _V + _C + 1; i++)
    {
        pid[i] = fork();
        if (pid[i] == 0)
            break;
    }
    // ======================================== Create actor process

    // Parent process ====================================================
    if (parent_pid == getpid())
    {

        // Wait for all the childeren=====================================
        for (int i = 0; i < _N + _V + _C + 1 || exit_requested != 0; i++)
        {
            int status;
            if (waitpid(pid[i], &status, 0) == -1)
            {
                errExit("waitpid");
            }
            //debug_printf("waitpid%d\n", i);
        }
        // =====================================Wait for all the childeren

        // Free resources
        free(pid);
        sem_destroy(&clinic->sem_shm_access);
        sem_destroy(&clinic->sem_full);
        sem_destroy(&clinic->sem_empty);
        sem_destroy(&clinic->sem_pair);
        sem_destroy(&clinic->sem_stored);
        shm_unlink(SHARED_LINK);
    }

    // Child processes ===================================================
    else
    {
        for (int i = 0; i < _N + _V + _C + 1; i++)
        {
            if (i >= 0 && i < _N && pid[i] == 0)
            {
                nurse(_I, clinic, i);
            }
            else if (i >= _N && i < _N + _V && pid[i] == 0)
            {
                vaccinator(clinic, i - _N);
            }
            else if (i >= _N + _V && i < _N + _V + _C && pid[i] == 0)
            {
                citizen(clinic, i - _N - _V);
            }
        }
    }
    // ===================================================================

    return 0;
}

void debug_printf(const char *format, ...)
{
    va_list args;
    va_start(args, format);

    if (DEBUG)
        vprintf(format, args);

    va_end(args);
}

void print_usage(void)
{
    printf("========================================\n");
    printf("Usage:\n"
           "./program [-n # of nurses] [-v # of vaccinators] [-c # of citizens] [-b  size of the buffer] [-t # of times each citizen must receive the 2 shots ] [-i input file path]\n");
    printf("Constraints(all are integers): \n"
           "n >= 2\n"
           "v >= 2\n"
           "c >= 3:\n"
           "b >= tc+1\n"
           "t >= 1\n");
    printf("========================================\n");
}

int validate_inputs()
{
    return (opt_N && opt_V && opt_C && opt_B && opt_T && opt_I) &&
           (_N >= 2 && _V >= 2 && _C >= 2 && _B >= _T * _C + 1 && _T >= 1);
}

void print_inputs(void)
{
    debug_printf("Inputs:\n"
                 "# of nurses(n):    %d\n"
                 "# of vaccinators(v): %d\n"
                 "# of citizens(c):   %d\n"
                 "size of the buffer(b):  %d\n"
                 "2 shots(t): %d\n"
                 "input file: %s\n",
                 _N, _V, _C, _B, _T, _I);
}

void sig_handler(int sig_no)
{
    exit_requested = sig_no;
}

void nurse(char *input_file, struct ClinicData *data, int id)
{
    int i_fd = open(input_file, O_RDONLY);

    char c;

    if (i_fd == -1)
        errExit("open @nurse()");

    while (exit_requested == 0)
    {
        s_wait(&data->sem_empty, "nurse_wait");
        s_wait(&data->sem_shm_access, "nurse_wait");

        if (data->total_carried >= _T * _C)
        {
            s_post(&data->sem_full, "nurse_post");
            s_post(&data->sem_empty, "nurse_wait");
            s_post(&data->sem_shm_access, "nurse_post");
            data->nurses_done++;
            if (data->nurses_done == _N)
                printf("Nurses have carried all vaccines to the buffer, terminating. %d\n", data->total_carried);

            break;
        }

        pread(i_fd, &c, 1, data->file_index++);

        if (c == '1')
        {
            data->pfizer++;
            if (data->sputnik > data->pfizer)
                s_post(&data->sem_pair, "nurse_post");
        }

        if (c == '2')
        {
            data->sputnik++;
            if (data->pfizer > data->sputnik)
                s_post(&data->sem_pair, "nurse_post");
        }

        data->total_carried++;

        printf("Nurse %d (pid=%d) has brought vaccine %c: the clinic has %d vaccine1 and %d vaccine2.\n", id, getpid(), c, data->pfizer, data->sputnik);

        s_post(&data->sem_shm_access, "nurse_post");
        s_post(&data->sem_full, "nurse_post");
    }

    free(pid);
    _exit(EXIT_SUCCESS);
}

void vaccinator(struct ClinicData *data, int id)
{
    while (exit_requested == 0)
    {
        s_wait(&data->sem_full, "vacc_wait");
        s_wait(&data->sem_shm_access, "vacc_wait");

        if (data->n_vaccinated >= _T * _C)
        {
            s_post(&data->sem_full, "vacc_post");
            s_post(&data->sem_shm_access, "vacc_post");
            break;
        }

        data->pfizer--;
        data->sputnik--;
        data->n_vaccinated++;

        s_post(&data->sem_shm_access, "vacc_post");
        s_post(&data->sem_empty, "vacc_post");
    }

    free(pid);
    _exit(EXIT_SUCCESS);
}
void citizen(struct ClinicData *data, int id)
{
    debug_printf("cite%d\n", id);
    free(pid);
    _exit(EXIT_SUCCESS);
}

int s_wait(sem_t *sem, char *msg)
{
    int ret;
    ret = sem_wait(sem);
    if (ret == -1)
        errExit(msg);

    return ret;
}
int s_post(sem_t *sem, char *msg)
{
    int ret;
    ret = sem_post(sem);
    if (ret == -1)
        errExit(msg);

    return ret;
}

int s_init(sem_t *sem, int val, char *msg)
{
    int ret;
    ret = sem_init(sem, 1, val);
    if (ret == -1)
        errExit(msg);

    return ret;
}