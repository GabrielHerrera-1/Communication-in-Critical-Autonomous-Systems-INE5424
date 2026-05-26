/*
 * gps.c --> modulo de kernel gps
 *
 * misc device /dev/gps (major 10, MISC_DYNAMIC_MINOR) com interface somente
 * via ioctl. simula deslocamento entre quadrantes adjacentes (grade 2x2):
 * a cada >= 3s move para um vizinho aleatorio usando jiffies como contador
 *
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/uaccess.h>
#include <linux/jiffies.h>
#include <linux/mutex.h>
#include <linux/random.h>

#include "gps_ioctl.h"

#define GPS_MOVE_INTERVAL  (3 * HZ)   /* troca de quadrante a cada >= 3s */

/* parametro do modulo: quadrante inicial fixo (0..3). -1 = aleatorio. */
static int initial_quadrant = -1;
/* 0444 é pra deixar só leitura */
module_param(initial_quadrant, int, 0444);
MODULE_PARM_DESC(initial_quadrant, "quadrante inicial 0..3 (default: aleatorio)");

/* estado da simulacao; ioctl pode ser concorrente --> mutex */
static DEFINE_MUTEX(gps_lock);
static int           quadrant;      /* quadrante atual: 0..3        */
static unsigned long last_change;   /* jiffies da ultima troca       */
static bool          fixed;         /* RSU: quadrante congelado      */

/*
 * grade 2x2:  Q0 | Q1
 *             -------
 *             Q2 | Q3
 * cada quadrante tem exatamente 2 vizinhos (sem diagonais)
 * Q0 nao pode ir direto para Q3; Q1 nao pode ir direto para Q2
 */
static const int gps_neighbors[GPS_QUADRANTS][2] = {
    {1, 2}, /* Q0 --> Q1, Q2 */
    {0, 3}, /* Q1: --> Q0, Q3 */
    {0, 3},  /* Q2: --> Q0, Q3 */
    {1, 2}, /* Q3: --> Q1, Q2 */
};

/* avanca para um quadrante adjacente se passou GPS_MOVE_INTERVAL. sob gps_lock */
static void gps_advance_locked(void)
{
    if (fixed)
        return;
    /* time_before trata overflow de jiffies */
    if (time_before(jiffies, last_change + GPS_MOVE_INTERVAL))
        return;
    quadrant    = gps_neighbors[quadrant][get_random_u32() % 2];
    last_change = jiffies;
}

static long gps_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
    int q;

    switch (cmd) {
    case GPS_IOC_GET_QUADRANT:
        mutex_lock(&gps_lock);
        /* verifica se ja passaram 3 seg e move para o vizinho aleatorio */
        gps_advance_locked();
        q = quadrant;
        mutex_unlock(&gps_lock);
        /* envia para o userspace */
        if (copy_to_user((void __user *)arg, &q, sizeof(q))) {
            /* significa bad address ou endereço invalido de memoria */
            return -EFAULT;
        }
        return 0;

    case GPS_IOC_SET_FIXED:
        mutex_lock(&gps_lock);
        fixed = true;
        mutex_unlock(&gps_lock);
        pr_info("gps: quadrante congelado em %d\n", quadrant);
        return 0;

    default:
        /* erro usado quando ioctl n é suportado pelo dispositivo */
        return -ENOTTY;
    }
}

/* 
owner --> diz que essa operacao pertence a esse modulo, kernel usa isso pra impedir rmod enquando alguem usa o driver por exemplo
unlocked_ioctl --> liga a syscall ioctl a funcao gps_ioctl, faz esse bind
*/
static const struct file_operations gps_fops = {
    .owner          = THIS_MODULE,
    .unlocked_ioctl = gps_ioctl,
};

/* major 10 (MISC_MAJOR): reservado pelo kernel para misc devices em
 * todas as distribuicoes. evita alocar um major proprio desnecessariamente */
static struct miscdevice gps_misc = {
    .minor = MISC_DYNAMIC_MINOR,
    .name  = "gps",
    .fops  = &gps_fops,
};

/* quando fazemos insmod gps.ko o kernel executa a função abaixo */
static int __init gps_init(void)
{
    /* para armazenar retorno do misc_register */
    int ret;

    if (initial_quadrant >= 0 && initial_quadrant < GPS_QUADRANTS)
        quadrant = initial_quadrant;
    else
        quadrant = get_random_u32() % GPS_QUADRANTS;
        
    last_change = jiffies;

    /* misc_register registra o device no kernel. reserva minor, conecta file operations,
    registra no vfs, cria entrada do device e integra com /dev
    */
    ret = misc_register(&gps_misc);
    if (ret)
        pr_err("gps: misc_register falhou: %d\n", ret);
    else
        pr_info("gps: carregado quadrante inicial %d\n", quadrant);
    return ret;
}

/* quando fazemos rmmod gps o kernel executa a função abaixo */
static void __exit gps_exit(void)
{
    /* remove /dev/gps, remove registro no VFS, remove callbacks e referencia miscdevice tb */
    misc_deregister(&gps_misc);
    pr_info("gps: descarregado\n");
}

/* conectam as funções ao kernel */
module_init(gps_init);
module_exit(gps_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Arthur Erpen, Caetano Peruzzo e Gabriel Herrera");
MODULE_DESCRIPTION("GPS virtual: sincronizacao espacial por quadrantes");
