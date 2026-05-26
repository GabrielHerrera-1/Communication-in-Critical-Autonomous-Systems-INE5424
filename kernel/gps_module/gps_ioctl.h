#ifndef GPS_IOCTL_H
#define GPS_IOCTL_H

/* interface compartilhada entre kernel e userspace 
   define os comandos ioctl, identificadores e constantes
*/

/* porque kernel e libc possuem headers diferentes */
#ifdef __KERNEL__
#include <linux/ioctl.h>
#else
#include <sys/ioctl.h>
#endif

#define GPS_QUADRANTS 4

/* magic number evita colisão entre ioctls diferentes */
#define GPS_IOC_MAGIC 'g'
/* le o quadrante atual da VM (0..3) e avanca a simulacao se passou o intervalo .
   IOR porque o userspace so le os dados encaminhados pelo kernel 
   parametros = magic, numero do comando, tipo transferido e direção (read nesse caso). essa combinação
   gera um inteiro unico de identificação
*/
#define GPS_IOC_GET_QUADRANT _IOR(GPS_IOC_MAGIC, 1, int)
/* congela o quadrante: a VM para de se deslocar (RSU, is_master) */
/* IO porque o userspace n precisa nem ler nem escrever dados no kernel */
#define GPS_IOC_SET_FIXED _IO(GPS_IOC_MAGIC, 2)

#endif /* GPS_IOCTL_H */
