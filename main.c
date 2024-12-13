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


static int child_running = 0;
void
pass()
{
    child_running = 1;
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
    kill(who, SIGUSR1);
    pause();
}

void
barrier_handler()
{
    ++BARRIER;
    kill(getppid(), SIGUSR1);
}

void
__wait()
{
    static int WAIT_COUNTER = 0;
    ++WAIT_COUNTER;
    while (BARRIER < WAIT_COUNTER)
        sched_yield();
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
do_parent_stuff(char *map_in, char *buf, int length, int new_length, int child)
{
    int length_med;
    int i;     // map_in index
    int j = 0; // buf index

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

    /* Manda al hijo una señal para que continue la ejecucion */
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

    /* Manda al hijo una señal para que continue la ejecucion */
    __awake(child);
}

void
do_child_stuff(char *buf, int length)
{
    int length_med;
    int i;
    int iterations;

    /* Espera a que el padre procese la primera mitad del archivo */
    __wait();

    length_med = (length + 1) >> 1;

    // Este codigo esta repetido abajo
    for (i = 0; i < length_med; ++i)
    {
        switch (buf[i])
        {
            case '0' ... '9':
                iterations = buf[i] - '0';
                for (int j = 0; j < iterations; ++j)
                    buf[i + j] = '*';
                break;
        }
    }

    /* Espera a que el padre procese la siguiente
     * mitad del archivo */
    __wait();

    // Este codigo esta repetido arriba
    for (; i < length; ++i)
    {
        switch (buf[i])
        {
            case '0' ... '9':
                iterations = buf[i] - '0';
                for (int j = 0; j < iterations; ++j)
                    buf[i + j] = '*';
                break;
        }
    }
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
    char       *shared_buffer;

    if (argc != 3)
    {
        fprintf(stderr, "Usage: %s <input_file> <output_file>\n", argv[0]);
        return 1;
    }

    if (!strcmp(argv[1], argv[2]))
    {
        fprintf(stderr, "Input and output file cant be the same\n");
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

    /* Calcula el nuevo tamaño del archivo */
    new_length = get_new_length(map_in, length);

    /* Cambia el tamaño del archivo de salida */
    if (ftruncate(out_fd, new_length))
    {
        perror("ftruncate");
        return 1;
    }

    /* Buffer compartido entre el padre y el hijo, donde el padre va a
     * escribir y el hijo va a leer y sobrescribir */
    shared_buffer = mmap(NULL, new_length, PROT_READ | PROT_WRITE,
                         MAP_SHARED | MAP_ANONYMOUS, -1, 0);

    /* Mapeo del archivo de salida */
    map_out = mmap(NULL, new_length, PROT_READ | PROT_WRITE, MAP_SHARED, out_fd, 0);
    if (map_out == MAP_FAILED)
    {
        perror("mmap 2");
        return 1;
    }

    /* Asigna una señal al padre (y al hijo) que ignora SIGUSR1 */
    if (signal(SIGUSR1, pass) == SIG_ERR)
    {
        perror("signal 1");
        return 1;
    }

    switch (child = fork())
    {
        case -1:
            perror("fork");
            exit(1);

        case 0: // child
            /* Set the handler */
            if (signal(SIGUSR1, barrier_handler) == SIG_ERR)
            {
                perror("signal 2");
                exit(0);
            }
            /* Say the parent that the handler is set yet */
            kill(getppid(), SIGUSR1);
            do_child_stuff(shared_buffer, new_length);
            exit(0);

        default: // parent
            /* Wait until the child set their handler */
            while (!child_running)
                pause();
            do_parent_stuff(map_in, shared_buffer, length, new_length, child);
            break;
    }

    memcpy(map_out, shared_buffer, new_length);

    if (munmap(map_in, length))
    {
        perror("munmap 1");
        return 1;
    }

    if (munmap(map_out, new_length))
    {
        perror("munmap 2");
        return 1;
    }

    return 0;
}
