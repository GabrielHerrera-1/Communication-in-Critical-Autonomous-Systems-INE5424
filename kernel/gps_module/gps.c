/*
 * gps.c - Modulo de kernel "GPS virtual" para a Etapa 4 (Sincronizacao Espacial).
 *
 * Char device /dev/gps com interface SOMENTE via ioctl (open/close/ioctl,
 * sem read/write). Simula o deslocamento da VM entre quadrantes: a cada
 * >= 3 segundos sorteia um novo quadrante (rand % 4). As coordenadas sao
 * inicialmente definidas de forma aleatoria.
 *
 * Nao cria threads nem timers: o sorteio acontece de forma preguicosa no
 * ioctl, comparando jiffies com o instante da ultima troca.
 *
 * insmod/ldmod chama gps_init() (module_init); rmmod chama gps_exit().
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/device.h>
#include <linux/uaccess.h>
#include <linux/jiffies.h>
#include <linux/mutex.h>
#include <linux/random.h>

#include "gps_ioctl.h"

#define GPS_DEV_NAME       "gps"
#define GPS_MOVE_INTERVAL  (3 * HZ)   /* troca de quadrante a cada >= 3s */

/* parametro do modulo: quadrante inicial fixo (0..3). -1 = aleatorio.
 * Util pro cenario de teste posicionar uma RSU em cada quadrante. */
static int initial_quadrant = -1;
module_param(initial_quadrant, int, 0444);
MODULE_PARM_DESC(initial_quadrant, "quadrante inicial 0..3 (default: aleatorio)");

static int gps_major;
static struct class *gps_class;
static struct device *gps_device;

/* estado da simulacao; ioctl pode ser concorrente => mutex */
static DEFINE_MUTEX(gps_lock);
static int quadrant;                /* quadrante atual: 0..3 */
static unsigned long last_change;   /* jiffies da ultima troca */
static bool fixed;                  /* RSU: quadrante congelado, nao se desloca */

/* sorteia um novo quadrante se ja passou GPS_MOVE_INTERVAL. sob gps_lock. */
static void gps_advance_locked(void)
{
    /* RSU (is_master): quadrante congelado */
    if (fixed)
        return;
    /* ainda nao passou o intervalo: permanece no mesmo quadrante */
    /* time_before é macro do kernel que trata overflow de jiffies */
    if (time_before(jiffies, last_change + GPS_MOVE_INTERVAL))
        return;

    quadrant = get_random_u32() % GPS_QUADRANTS;   /* rand % 4 */
    last_change = jiffies;
}

static int gps_open(struct inode *inode, struct file *file)
{
    return 0;
}

static int gps_release(struct inode *inode, struct file *file)
{
    return 0;
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
        /* RSU congela o quadrante: o estado e global ao modulo, entao todos
         * os processos da VM passam a ver o mesmo quadrante fixo. */
        mutex_lock(&gps_lock);
        fixed = true;
        mutex_unlock(&gps_lock);
        pr_info("gps: quadrante congelado em %d\n", quadrant);
        return 0;

    default:
        return -ENOTTY;
    }
}

/* nosso GPS so sabe fazer open, close e ioctl */
static const struct file_operations gps_fops = {
    .owner          = THIS_MODULE,
    .open           = gps_open,
    .release        = gps_release,
    .unlocked_ioctl = gps_ioctl,
};

static int __init gps_init(void)
{
    /* register_chrdev avisa o kernel o que sabemos fazer (gps_fops).
     * major 0 => o kernel aloca um major livre e devolve. */
    gps_major = register_chrdev(0, GPS_DEV_NAME, &gps_fops);
    if (gps_major < 0) {
        pr_err("gps: register_chrdev falhou: %d\n", gps_major);
        return gps_major;
    }

    /* cria a classe + device para o devtmpfs gerar /dev/gps sozinho */
    gps_class = class_create(GPS_DEV_NAME);
    if (IS_ERR(gps_class)) {
        unregister_chrdev(gps_major, GPS_DEV_NAME);
        return PTR_ERR(gps_class);
    }

    gps_device = device_create(gps_class, NULL, MKDEV(gps_major, 0),
                               NULL, GPS_DEV_NAME);
    if (IS_ERR(gps_device)) {
        class_destroy(gps_class);
        unregister_chrdev(gps_major, GPS_DEV_NAME);
        return PTR_ERR(gps_device);
    }

    /* quadrante inicial: aleatorio por padrao, ou forcado pelo param */
    if (initial_quadrant >= 0 && initial_quadrant < GPS_QUADRANTS) {
        quadrant = initial_quadrant;
    } else {
        quadrant = get_random_u32() % GPS_QUADRANTS;
    }
    last_change = jiffies;

    pr_info("gps: carregado (major=%d) quadrante inicial %d\n",
            gps_major, quadrant);
    return 0;
}

static void __exit gps_exit(void)
{
    device_destroy(gps_class, MKDEV(gps_major, 0));
    class_destroy(gps_class);
    unregister_chrdev(gps_major, GPS_DEV_NAME);
    pr_info("gps: descarregado\n");
}

module_init(gps_init);
module_exit(gps_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("SO2 Etapa 4");
MODULE_DESCRIPTION("GPS virtual: sincronizacao espacial por quadrantes");
