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

void
pass()
{
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
do_parent_stuff(char *map_in, char *map_out, int length, int new_length)
{
    char *buf;
    int   length_med;
    int   i;     // map_in index
    int   j = 0; // buf index
    int   mid_j;


    if (!(buf = malloc(new_length)))
    {
        perror("malloc 1");
        return;
    }

    memset(buf, 0, new_length);

    length_med = (length + 1) / 2;

#define do_the_stuff()                          \
    switch (map_in[i])                          \
    {                                           \
        case '0' ... '9':                       \
            buf[j] = map_in[i];                 \
            j += map_in[i] - '0';               \
            break;                              \
                                                \
        case 'A' ... 'Z':                       \
            buf[j++] = map_in[i] - ('A' - 'a'); \
            break;                              \
                                                \
        default:                                \
            buf[j++] = map_in[i];               \
    }

    for (i = 0; i < length_med; ++i)
    {
        do_the_stuff();
    }
    mid_j = j;
    memcpy(map_out, buf, mid_j);

    for (; i < length; ++i)
    {
        do_the_stuff();
    }

    memcpy(map_out + mid_j, buf + mid_j, new_length - mid_j);

#undef do_the_stuff
    free(buf);
}

void
do_child_stuff(char *map_out, int length)
{
    char *buf;
    int   length_med;
    int   i;
    int   iterations;

    if (!(buf = malloc(length)))
    {
        perror("malloc 1");
        return;
    }

    memcpy(buf, map_out, length);

    length_med = (length + 1) / 2;

#define do_the_stuff()                           \
    switch (map_out[i])                          \
    {                                            \
        case '0' ... '9':                        \
            iterations = map_out[i] - '0';       \
            for (int j = 0; j < iterations; ++j) \
                buf[i++] = '*';                  \
            break;                               \
    }

    for (i = 0; i < length_med; ++i)
    {
        do_the_stuff();
    }

    memcpy(map_out, buf, length_med);

    for (; i < length; ++i)
    {
        do_the_stuff();
    }

    memcpy(map_out + length_med, buf + length_med, length - length_med);

#undef do_the_stuff
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
        printf("Usage: %s <input_file> <output_file>\n", argv[0]);
        return 1;
    }

    if (!strcmp(argv[1], argv[2]))
    {
        printf("Input and output file cant be the same\n");
        return 1;
    }

    if ((in_fd = open(argv[1], O_RDONLY)) < 0)
    {
        perror("open 1");
        return 1;
    }

    if ((out_fd = open(argv[2], O_RDWR | O_TRUNC | O_CREAT, (mode_t) 0666)) < 0)
    {
        perror("open 2");
        return 1;
    }

    if (fstat(in_fd, &in_stat))
    {
        perror("fstat 1");
        return 1;
    }

    length = in_stat.st_size;

    map_in = mmap(NULL, length, PROT_READ, MAP_SHARED, in_fd, 0);
    if (map_in == MAP_FAILED)
    {
        perror("mmap 1");
        return 1;
    }

    new_length = get_new_length(map_in, length);

    if (ftruncate(out_fd, new_length))
    {
        perror("ftruncate");
        return 1;
    }


    map_out = mmap(NULL, new_length, PROT_READ | PROT_WRITE, MAP_SHARED, out_fd, 0);
    if (map_out == MAP_FAILED)
    {
        perror("mmap 2");
        return 1;
    }

    signal(SIGUSR1, pass);

    switch (child = fork())
    {
        case -1:
            perror("fork");
            exit(1);

        case 0: // child
            kill(getppid(), SIGUSR1);
            pause();
            do_child_stuff(map_out, new_length);
            exit(0);

        default: // parent
            pause();
            kill(child, SIGUSR1);
            do_parent_stuff(map_in, map_out, length, new_length);
            break;
    }
    return 0;
}
