#ifndef _SC_PCI_PUB_H
#define _SC_PCI_PUB_H

#include <linux/crc32.h>
#include <linux/delay.h>
#include <linux/fs.h>
#include <linux/io.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/random.h>
#include <linux/slab.h>
#include <linux/pci.h>
#include <linux/pci_ids.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/proc_fs.h>
#include <linux/pci_regs.h>
#include <uapi/linux/pcitest.h>
//#include "sc_pci_vnet.h"


#define DRV_MODULE_NAME				"sc_pci_pub"

enum pci_barno {
	BAR_0,
	BAR_1,
	BAR_2,
	BAR_3,
	BAR_4,
	BAR_5,
};

struct sc_pci_pub {
	struct pci_dev *pdev;
	void  __iomem	*mmio_bar5;
	void 		  *sc_pci_ep_serial_priv;
};


extern void sc_pci_endpoint_serial_remove(struct pci_dev *pdev);
extern int sc_pci_endpoint_serial_probe(struct pci_dev *pdev);
extern int sc_pci_vnet_probe(struct pci_dev *pdev);

#endif
