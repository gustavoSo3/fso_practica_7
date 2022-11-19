#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <mmu.h>

#define NUMPROCS 4
#define PAGESIZE 4096
#define PHISICALMEMORY 12 * PAGESIZE
#define TOTFRAMES PHISICALMEMORY / PAGESIZE                    // 12
#define RESIDENTSETSIZE PHISICALMEMORY / (PAGESIZE * NUMPROCS) // 3 = (12 * 4096)/(4096 * 4) = 12/4

extern char *base;
extern int framesbegin;
extern int idproc;
extern int systemframetablesize;
extern int ptlr; // tamaño de tabla de procesos

extern struct SYSTEMFRAMETABLE *systemframetable; // tabla de marcos del sistema
extern struct PROCESSPAGETABLE *ptbr;             // tabla de paginas del proceso

int getfreeframe();
int searchvirtualframe();
int getLRU();
int getfifo();

int pagefault(char *vaddress)
{
    int i;
    int frame, vframe;
    long pag_a_expulsar;
    int fd;
    char buffer[PAGESIZE];
    int pag_del_proceso;
    // printf("systemframetablesize, %d\nframesbegin, %d\n",systemframetablesize,framesbegin);

    // A partir de la dirección que provocó el fallo, calculamos la página
    pag_del_proceso = (long)vaddress >> 12;

    // Si la página del proceso está en un marco virtual del disco (memoria secundaria)
    if (ptbr[pag_del_proceso].framenumber >= TOTFRAMES) // para estar en un marco virtual su numero debe ser mayor al total de frames fisicos
    {

        // Lee el marco virtual al buffer
        readblock(buffer, ptbr[pag_del_proceso].framenumber);

        // Libera el frame virtual
        systemframetable[ptbr[pag_del_proceso].framenumber].assigned = 0;
        ptbr[pag_del_proceso].framenumber = -1;
    }

    // Cuenta los marcos asignados al proceso
    i = countframesassigned();

    // Si ya ocupó todos sus marcos, expulsa una página del proceso
    if (i >= RESIDENTSETSIZE)
    {
        // Buscar una página a expulsar
        pag_a_expulsar = getLRU(); // getfifo();
        // Poner el bit de presente en 0 en la tabla de páginas
        ptbr[pag_a_expulsar].presente = 0;

        // Si la página ya fue modificada, grábala en disco
        if (ptbr[pag_a_expulsar].modificado)
        {
            // Escribe el frame de la página en el archivo de respaldo y pon en 0 el bit de modificado
            saveframe(ptbr[pag_a_expulsar].framenumber);
            ptbr[pag_a_expulsar].modificado = 0;
        }

        // Busca un frame virtual (disponible) en memoria secundaria
        vframe = searchvirtualframe();
        // Si no hay frames virtuales en memoria secundaria regresa error
        if (vframe == -1)
        {
            return (-1);
        }
        // Copia el frame (de la pagina a expulsar) a memoria secundaria, actualiza la tabla de páginas y libera el marco de la memoria principal
        copyframe(ptbr[pag_a_expulsar].framenumber, vframe);             // copia
        systemframetable[ptbr[pag_a_expulsar].framenumber].assigned = 0; // libera
        ptbr[pag_a_expulsar].framenumber = vframe;                       // actualiza
    }

    // Busca un marco físico libre en el sistema
    frame = getfreeframe();
    // Si no hay marcos físicos libres en el sistema regresa error
    if (frame == -1)
    {
        return (-1); // Regresar indicando error de memoria insuficiente
    }

    // Si la página estaba en memoria secundaria
    if (ptbr[pag_del_proceso].framenumber == -1) // en caso de haber estado en memoria secundaria se asignaba el valor de -1;
    {
        // Cópialo (el bufer) al frame libre encontrado en memoria principal y transfiérelo a la memoria física
        writeblock(buffer, frame);
        loadframe(frame);
    }

    // Poner el bit de presente en 1 en la tabla de páginas y el frame
    ptbr[pag_del_proceso].framenumber = frame;
    ptbr[pag_del_proceso].presente = 1;

    return (1); // Regresar todo bien
}

int getLRU()
{
    int res = -1;
    unsigned long mintime = 0;
    for (int i = 0; i < ptlr; i++)
    {
        if (ptbr[i].presente && (res == -1 || (ptbr[i].tlastaccess < mintime)))
        {
            mintime = ptbr[i].tlastaccess;
            res = i;
        }
    }
    return res;
}

int fifo = 0;
int getfifo()
{
    int t = fifo;
    fifo = (fifo + 1) % ptlr;
    return t;
}
int searchvirtualframe()
{
    for (int i = systemframetablesize; i < systemframetablesize * 2; i++)
        if (!systemframetable[i].assigned)
        {
            systemframetable[i].assigned = 1;
            return i;
        }
    return -1;
}

int getfreeframe()
{
    for (int i = 0; i < systemframetablesize; i++)
        if (!systemframetable[i].assigned)
        {
            systemframetable[i].assigned = 1;
            return i;
        }
    return -1;
}