#include <fcntl.h>
#include <sched.h>
#include <signal.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#define V_SILENT 1
#define V_SYNC   2
#define V_NOERR  4
#define V_INFO   8
/* Verbose: print debug messages
 * V_SILENT: none except error
 * V_SYNC: sync msg
 * V_NOERR: do no show if(VERBOSE &~ V_NOERR)perror msg
 * V_INFO: some msg
 */
#define VERBOSE (V_SILENT)

void
pass()
{
}

/* Sistema de barreras para manejar la sincronizacion entre procesos
 * El padre sera el que ejecute el awake
 * El hijo sera el que espere a ser despertado.
 *
 * __awake(): Permite al hijo avanzar
 * __wait(): Espera hasta que se hayan llamado al menos
 * el mismo numero de veces a __awake() que a __wait()
 *
 * Beneficio: El hijo puede ejecutar secciones de codigo siempre que el
 * padre haya ejecutado el awake que le permite continuar la ejecucucion.
 * Si el padre llama dos o mas veces a __awake() sin que el hijo este
 * esperando, cuando este recupere la cpu saltara tantas llamadas a
 * __wait() como veces se haya llamado a __awake().
 *
 * Nota: Dos kills consecutivos con la misma signal pueden ser leidos
 * como uno solo, por lo que __awake manda un SIGUSR1 que es leido
 * por barrier_handler y espera a que este le devuelva otro SIGUSR1,
 * a modo de notificacion que indica que barrier ya ha sido modificada.
 */
static int BARRIER = 0;

void
__awake(int who)
{
    if (VERBOSE & V_SYNC)
        puts("SEND AWAKE");
    kill(who, SIGUSR1);
    pause();
    if (VERBOSE & V_SYNC)
        puts("AWAKE DONE");
}

void
barrier_handler()
{
    if (VERBOSE & V_SYNC)
        puts("BARRIER INC");
    ++BARRIER;
    kill(getppid(), SIGUSR1);
}

void
__wait()
{
    if (VERBOSE & V_SYNC)
        puts("WAITING");
    static int WAIT_COUNTER = 0;
    ++WAIT_COUNTER;
    while (BARRIER < WAIT_COUNTER)
        sched_yield();
    if (VERBOSE & V_SYNC)
        puts("CONTINUING");
}

int
get_new_length(char *map, int length)
{
    int new_length = length;

    for (int i = 0; i < length; ++i)

        if ('0' <= map[i] && map[i] <= '9')
            new_length += map[i] - '1';

    return new_length;
}

void
do_parent_stuff(char *map_in, char *map_out, int length, int new_length, int child)
{
    char *buf;
    int   length_med;
    int   i;     // map_in index
    int   j = 0; // buf index
    int   mid_j;


    if (!(buf = malloc(new_length)))
    {
        if (VERBOSE & ~V_NOERR)
            perror("malloc 1");
        return;
    }

    memset(buf, 0, new_length);

    length_med = (length + 1) >> 1;


    for (i = 0; i < length_med; ++i)
    {
        switch (map_in[i])
        {
            case '0' ... '9':
                buf[j] = map_in[i];
                j += map_in[i] - '0';
                break;

            case 'A' ... 'Z':
                buf[j++] = map_in[i] - ('A' - 'a');
                break;

            default:
                buf[j++] = map_in[i];
        }
    }

    if (VERBOSE & V_INFO)
    {
        puts("PARENT 1st ITERATION:");
        for (i = 0; i < length_med; ++i)
            putchar(buf[i]);
        puts("");
    }
    mid_j = j;

    memcpy(map_out, buf, mid_j + 1);
    __awake(child);

    for (i = length_med; i < length; ++i)
    {
        switch (map_in[i])
        {
            case '0' ... '9':
                buf[j] = map_in[i];
                j += map_in[i] - '0';
                break;

            case 'A' ... 'Z':
                buf[j++] = map_in[i] - ('A' - 'a');
                break;

            default:
                buf[j++] = map_in[i];
        }
    }

    if (VERBOSE & V_INFO)
    {
        puts("PARENT 2nd ITERATION:");
        for (i = length_med; i < length; ++i)
            putchar(buf[i]);
        puts("");
    }

    memcpy(map_out + mid_j, buf + mid_j, new_length - mid_j);
    __awake(child);

    free(buf);
}

void
do_child_stuff(char *map_out, int length)
{
    char *buf;
    int   length_med;
    int   i;
    int   iterations;

    __wait();

    if (VERBOSE & V_INFO)
        printf("CHILD MAP-OUT LEN: %d\n", length);


    if (!(buf = malloc(length)))
    {
        if (VERBOSE & ~V_NOERR)
            perror("malloc 1");
        return;
    }


    length_med = (length + 1) >> 1;

    memcpy(buf, map_out, length_med);

    for (i = 0; i < length_med; ++i)
    {
        switch (map_out[i])
        {
            case '0' ... '9':
                iterations = map_out[i] - '0';
                for (int j = 0; j < iterations; ++j)
                    buf[i + j] = '*';
                break;
        }
    }

    if (VERBOSE & V_INFO)
    {
        puts("CHILD 1st ITERATION:");
        for (i = 0; i < length_med; ++i)
            putchar(buf[i]);
        puts("");
    }

    memcpy(map_out, buf, length_med);
    if (VERBOSE & V_INFO)
        printf("MEMCPY map-out <- buf [%d bytes]\n", length_med);

    __wait();
    memcpy(buf + length_med, map_out + length_med, length - length_med);

    for (; i < length; ++i)
    {
        switch (map_out[i])
        {
            case '0' ... '9':
                iterations = map_out[i] - '0';
                for (int j = 0; j < iterations; ++j)
                    buf[i + j] = '*';
                break;
        }
    }

    if (VERBOSE & V_INFO)
    {
        puts("CHILD 2nd ITERATION:");
        for (i = length_med; i < length; ++i)
            putchar(buf[i]);
        puts("");
    }

    memcpy(map_out + length_med, buf + length_med, length - length_med);
    if (VERBOSE & V_INFO)
        printf("MEMCPY map-out+%d <- buf+%d [%d bytes]\n", length_med,
               length_med, length - length_med);

    free(buf);
}

int
main(int argc, char *argv[])
{
    struct stat in_stat;
    int         child;
    int         in_fd;
    int         out_fd;
    int         length;
    int         new_length;
    char       *map_in;
    char       *map_out;

    if (argc != 3)
    {
        if (VERBOSE & ~V_NOERR)
            fprintf(stderr, "Usage: %s <input_file> <output_file>\n", argv[0]);
        return 1;
    }

    if (!strcmp(argv[1], argv[2]))
    {
        if (VERBOSE & ~V_NOERR)
            fprintf(stderr, "Input and output file cant be the same\n");
        return 1;
    }

    if ((in_fd = open(argv[1], O_RDONLY)) < 0)
    {
        if (VERBOSE & ~V_NOERR)
            perror("open 1");
        return 1;
    }

    if ((out_fd = open(argv[2], O_RDWR | O_TRUNC | O_CREAT, (mode_t) 0666)) < 0)
    {
        if (VERBOSE & ~V_NOERR)
            perror("open 2");
        return 1;
    }

    if (fstat(in_fd, &in_stat))
    {
        if (VERBOSE & ~V_NOERR)
            perror("fstat 1");
        return 1;
    }

    length = in_stat.st_size;
    if (VERBOSE & V_INFO)
        printf("ORIGINAL LENGTH: %d\n", length);

    map_in = mmap(NULL, length, PROT_READ, MAP_SHARED, in_fd, 0);
    if (map_in == MAP_FAILED)
    {
        if (VERBOSE & ~V_NOERR)
            perror("mmap 1");
        return 1;
    }

    new_length = get_new_length(map_in, length);
    if (VERBOSE & V_INFO)
        printf("NEW LENGTH: %d\n", new_length);

    if (ftruncate(out_fd, new_length))
    {
        if (VERBOSE & ~V_NOERR)
            perror("ftruncate");
        return 1;
    }


    map_out = mmap(NULL, new_length, PROT_READ | PROT_WRITE, MAP_SHARED, out_fd, 0);
    if (map_out == MAP_FAILED)
    {
        if (VERBOSE & ~V_NOERR)
            perror("mmap 2");
        return 1;
    }

    if (signal(SIGUSR1, pass) == SIG_ERR)
    {
        if (VERBOSE & ~V_NOERR)
            perror("signal 1");
        return 1;
    }

    switch (child = fork())
    {
        case -1:
            if (VERBOSE & ~V_NOERR)
                perror("fork");
            exit(1);

        case 0: // child
            if (VERBOSE & V_SYNC)
                puts("SETTING HANDLER");
            if (signal(SIGUSR1, barrier_handler) == SIG_ERR)
            {
                if (VERBOSE & ~V_NOERR)
                    perror("signal 2");
                exit(0);
            }
            kill(getppid(), SIGUSR1);
            if (VERBOSE & V_SYNC)
                puts("DOING CHILD STUFF");
            do_child_stuff(map_out, new_length);
            exit(0);

        default: // parent
            if (VERBOSE & V_SYNC)
                puts("WAITING UNTIL HANDLER IS SET");
            pause();
            if (VERBOSE & V_SYNC)
                puts("DOING PARENT STUFF");
            do_parent_stuff(map_in, map_out, length, new_length, child);
            break;
    }

    if (munmap(map_in, length))
    {
        if (VERBOSE & ~V_NOERR)
            perror("munmap 1");
        return 1;
    }

    if (munmap(map_out, new_length))
    {
        if (VERBOSE & ~V_NOERR)
            perror("munmap 2");
        return 1;
    }

    return 0;
}
