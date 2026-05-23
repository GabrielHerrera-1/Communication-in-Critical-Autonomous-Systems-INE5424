/*
 * gps.c - Modulo de kernel "GPS virtual" para a Etapa 4 (Sincronizacao Espacial).
 *
 * Misc device /dev/gps (major 10, MISC_DYNAMIC_MINOR) com interface somente
 * via ioctl. Simula deslocamento entre quadrantes adjacentes (grade 2x2):
 * a cada >= 3s move para um vizinho aleatorio usando jiffies como contador.
 *
 * Sem threads, sem timers: avanco pregicoso no proprio ioctl GET_QUADRANT.
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
module_param(initial_quadrant, int, 0444);
MODULE_PARM_DESC(initial_quadrant, "quadrante inicial 0..3 (default: aleatorio)");

/* estado da simulacao; ioctl pode ser concorrente => mutex */
static DEFINE_MUTEX(gps_lock);
static int           quadrant;      /* quadrante atual: 0..3        */
static unsigned long last_change;   /* jiffies da ultima troca       */
static bool          fixed;         /* RSU: quadrante congelado      */

/*
 * Grade 2x2:  Q0 | Q1
 *             -------
 *             Q2 | Q3
 * Cada quadrante tem exatamente 2 vizinhos (sem diagonais).
 * Q0 nao pode ir direto para Q3; Q1 nao pode ir direto para Q2.
 */
static const int gps_neighbors[GPS_QUADRANTS][2] = {
    {1, 2},  /* Q0: vizinhos Q1, Q2 */
    {0, 3},  /* Q1: vizinhos Q0, Q3 */
    {0, 3},  /* Q2: vizinhos Q0, Q3 */
    {1, 2},  /* Q3: vizinhos Q1, Q2 */
};

/* avanca para um quadrante adjacente se passou GPS_MOVE_INTERVAL. sob gps_lock. */
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
        gps_advance_locked();
        q = quadrant;
        mutex_unlock(&gps_lock);
        if (copy_to_user((void __user *)arg, &q, sizeof(q)))
            return -EFAULT;
        return 0;

    case GPS_IOC_SET_FIXED:
        mutex_lock(&gps_lock);
        fixed = true;
        mutex_unlock(&gps_lock);
        pr_info("gps: quadrante congelado em %d\n", quadrant);
        return 0;

    default:
        return -ENOTTY;
    }
}

static const struct file_operations gps_fops = {
    .owner          = THIS_MODULE,
    .unlocked_ioctl = gps_ioctl,
};

/* major 10 (MISC_MAJOR): reservado pelo kernel para misc devices em
 * todas as distribuicoes. Evita alocar um major proprio desnecessariamente. */
static struct miscdevice gps_misc = {
    .minor = MISC_DYNAMIC_MINOR,
    .name  = "gps",
    .fops  = &gps_fops,
};

static int __init gps_init(void)
{
    int ret;

    if (initial_quadrant >= 0 && initial_quadrant < GPS_QUADRANTS)
        quadrant = initial_quadrant;
    else
        quadrant = get_random_u32() % GPS_QUADRANTS;
    last_change = jiffies;

    ret = misc_register(&gps_misc);
    if (ret)
        pr_err("gps: misc_register falhou: %d\n", ret);
    else
        pr_info("gps: carregado quadrante inicial %d\n", quadrant);
    return ret;
}

static void __exit gps_exit(void)
{
    misc_deregister(&gps_misc);
    pr_info("gps: descarregado\n");
}

module_init(gps_init);
module_exit(gps_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("SO2 Etapa 4");
MODULE_DESCRIPTION("GPS virtual: sincronizacao espacial por quadrantes");
