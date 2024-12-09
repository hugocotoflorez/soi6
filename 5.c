#include <fcntl.h>
#include <sched.h>
#include <signal.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>


static int barrier = 0;

void
do_nothing()
{
}

void
barrier_sighandler()
{
    ++barrier;
    kill(getppid(), SIGUSR1);
}


void
barrier_wait(int barrier_num)
{
    while (barrier < barrier_num)
        sched_yield();
}

int
parse_nums(int in_fd)
{
    struct stat in_stat;
    char       *map;
    int         temp;
    int         length;
    char       *buf;

    if (fstat(in_fd, &in_stat))
    {
        perror("fstat1");
        barrier_wait(1);
        barrier_wait(2);
        barrier_wait(3);
        return 1;
    }
    length = in_stat.st_size;

    buf = malloc(length);
    if (!buf)
    {
        perror("malloc");
        barrier_wait(1);
        barrier_wait(2);
        barrier_wait(3);
        return 1;
    }

    /* Espera a que el padre le mande empezar */
    barrier_wait(1);

    map = mmap(NULL, length, PROT_WRITE | PROT_READ, MAP_SHARED, in_fd, 0);

    if (map == MAP_FAILED)
    {
        perror("mmap1");
        barrier_wait(2);
        barrier_wait(3);
        return 1;
    }

    for (int i = 0; i < length; ++i)
    {
        switch (map[i])
        {
            case '1' ... '9':
                temp = map[i] - '0';

                memset(buf + i, '*', temp);
                i += temp - 1;
                break;

            default:
                buf[i] = map[i];
                break;
        }
    }

    barrier_wait(2);
    if (fstat(in_fd, &in_stat))
    {
        perror("fstat2");
        barrier_wait(3);
        return 1;
    }
    if (in_stat.st_size > length)
    {
        length = in_stat.st_size;
        map    = realloc(map, length);
    }

    for (int i = 0; i < length; ++i)
    {
        switch (map[i])
        {
            case '1' ... '9':
                temp = map[i] - '0';

                memset(buf + i, '*', temp);
                i += temp - 1;
                break;

            default:
                buf[i] = map[i];
                break;
        }
    }


    /* Espera para que el output del pade salga en pantalla primero */
    barrier_wait(3);


    printf("CHILD BUF:\n");
    for (int i = 0; i < length; ++i)
        putchar(buf[i]);
    puts("");

    /* Esto no se puede hacer, no se guarda */
    memcpy(map, buf, length);

    if (munmap(map, length))
    {
        perror("munmap");
        return -1;
    }

    return 0;
}


void
barrier_inc(int child)
{
    kill(child, SIGUSR1);
    pause();
}


int
parse_alpha(int in_fd, int length, int child, int out_fd)
{
    char *map;
    int   new_len = length;
    char *buf;
    int   temp;
    int   right_padd = 0;
    int   midlen     = 0;

    buf = calloc(length, sizeof(char));
    if (!buf)
    {
        perror("malloc");
        return 1;
    }

    map = mmap(NULL, length, PROT_READ | PROT_WRITE, MAP_SHARED, in_fd, 0);
    if (map == MAP_FAILED)
    {
        perror("mmap2");
        return -1;
    }

    for (int i = 0, j = 0; i < length; ++i, ++j)
    {
        switch (map[i])
        {
            case 'A' ... 'Z':
                buf[j] = map[i] - ('A' - 'a');
                break;

            case '0':
                ++right_padd;
                break;

            case '1' ... '9':
                temp = map[i] - '0' - 1;
                new_len += temp;
                if (!(buf = realloc(buf, new_len)))
                {
                    perror("realloc");
                    return 1;
                }

                buf[j] = map[i];
                /* Se puede quitar si no se va a usar sizeof buffer */
                memset(buf + j + 1, ' ', temp);
                j += temp;
                break;

            default:
                buf[j] = map[i];
                break;
        }

        if (i == length / 2)
        {
            midlen = j;
            if (midlen > length)
                ftruncate(out_fd, new_len);

            write(out_fd, buf, midlen);
            fsync(out_fd);
            barrier_inc(child);
        }
    }

    ftruncate(out_fd, new_len);
    write(out_fd, buf + midlen, new_len - midlen);
    fsync(out_fd);

    /* Say child that parent end the task */
    barrier_inc(child);

    printf("PARENT BUF:\n");
    for (int i = 0; i < new_len; ++i)
        putchar(buf[i]);
    puts("");

    /* Say child that can write */
    barrier_inc(child);

    if (munmap(map, length))
    {
        perror("munmap");
        return -1;
    }

    return new_len;
}

int
parse_files(const char *in_filename, const char *out_filename)
{
    struct stat in_stat;
    int         in_fd;
    int         out_fd;
    int         child;

    if ((in_fd = open(in_filename, O_RDWR), 0666) < 0)
    {
        perror("open (in_fd)");
        return 1;
    }

    if ((out_fd = open(out_filename, O_RDWR | O_CREAT | O_TRUNC), 0666) < 0)
    {
        perror("open (out_fd)");
        return 1;
    }

    if (fstat(in_fd, &in_stat))
    {
        perror("fstat");
        return 1;
    }

    switch (child = fork())
    {
        case -1:
            perror("fork");
            return 1;

        case 0:
            if (signal(SIGUSR1, barrier_sighandler) == SIG_ERR)
            {
                perror("Signal");
                return 1;
            }
            /* Awake the parent as the handler is set yet */
            kill(getppid(), SIGUSR1);
            kill(getppid(), SIGUSR1);
            kill(getppid(), SIGUSR1);
            kill(getppid(), SIGUSR1);
            exit(parse_nums(out_fd));

        default:
            pause(); // wait until handler is set
            parse_alpha(in_fd, in_stat.st_size, child, out_fd);
            wait(NULL);
            break;
    }
    return 0;
}

int
main(int argc, char *argv[])
{
    if (argc != 3)
    {
        printf("Usage: %s <input_file> <output_file>\n", argv[0]);
        return 1;
    }

    if (!strcmp(argv[1], argv[2]))
    {
        printf("Input and output file cant be the same\n");
        return 1;
    }

    if (signal(SIGUSR1, do_nothing) == SIG_ERR)
    {
        perror("Signal");
        return 1;
    }

    return parse_files(argv[1], argv[2]);
}
