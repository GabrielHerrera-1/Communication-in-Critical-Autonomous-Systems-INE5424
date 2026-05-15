#ifndef GPS_IOCTL_H
#define GPS_IOCTL_H

/* Interface compartilhada entre o modulo de kernel GPS e o userspace. */

#ifdef __KERNEL__
#include <linux/ioctl.h>
#else
#include <sys/ioctl.h>
#endif

#define GPS_QUADRANTS 4

#define GPS_IOC_MAGIC        'g'
/* le o quadrante atual da VM (0..3); avanca a simulacao se passou o intervalo */
#define GPS_IOC_GET_QUADRANT _IOR(GPS_IOC_MAGIC, 1, int)
/* congela o quadrante: a VM para de se deslocar (RSU, is_master) */
#define GPS_IOC_SET_FIXED    _IO(GPS_IOC_MAGIC, 2)

#endif /* GPS_IOCTL_H */
