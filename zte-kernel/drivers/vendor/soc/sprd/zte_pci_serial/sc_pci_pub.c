// SPDX-License-Identifier: GPL-2.0-only
/**
 * Host side test driver to test endpoint functionality
 *
 * Copyright (C) 2017 Texas Instruments
 * Author: Kishon Vijay Abraham I <kishon@ti.com>
 */
#include "sc_pci_pub.h"

#define SC_PCIE_PUB_DEBUG
#ifdef SC_PCIE_PUB_DEBUG
#define sc_pcie_pub_dbg(fmt, ...) printk("[sc_pcie_pub_dbg epf]:[%s-%d] " fmt, __func__, __LINE__, ##__VA_ARGS__)
#else
#define sc_pcie_pub_dbg(fmt, ...)
#endif


static int sc_pci_pub_probe(struct pci_dev *pdev,
				   const struct pci_device_id *pci_id)
{
	struct sc_pci_pub *sc_pci_pub_priv;
	int ret;

	dev_info(&pdev->dev, "%s: PCIe VID: 0x%x DID: 0x%x\n", __func__,
		pci_id->vendor, pci_id->device);

	sc_pci_pub_priv = kzalloc(sizeof(struct sc_pci_pub), GFP_KERNEL);

	sc_pci_pub_priv->pdev = pdev;
	pci_set_drvdata(pdev, sc_pci_pub_priv);

	ret = pci_enable_device(pdev);
	if (ret) {
		dev_err(&pdev->dev, "pci_enable_device() failed: %d\n", ret);
	}

	pci_set_master(pdev);

	ret = pci_alloc_irq_vectors(pdev, 8, 8, PCI_IRQ_MSI);
	if (ret <= 0) {
		dev_err(&pdev->dev, "pci_alloc_irq_vectors() fail: %d\n", ret);
		ret = -EIO;
	}

	sc_pci_pub_priv->mmio_bar5 = pci_ioremap_bar(pdev, BAR_5); /* int space */
	if (!sc_pci_pub_priv->mmio_bar5) {
		ret = -ENOMEM;
		dev_err(&pdev->dev, "Failed to read BAR%d\n", BAR_5);
	}
	sc_pcie_pub_dbg("bar%d:base:0x%llx, size:%x",
					BAR_5, sc_pci_pub_priv->mmio_bar5, pci_resource_len(pdev, BAR_5));

	sc_pci_vnet_probe(pdev);
	sc_pci_endpoint_serial_probe(pdev);


	return 0;
}

static void sc_pci_pub_remove(struct pci_dev *pdev)
{
#if 0
	int id;
	enum pci_barno bar;
	struct pci_endpoint_test *test = pci_get_drvdata(pdev);
	struct miscdevice *misc_device = &test->miscdev;

	if (sscanf(misc_device->name, DRV_MODULE_NAME ".%d", &id) != 1)
		return;
	if (id < 0)
		return;

	misc_deregister(&test->miscdev);
	kfree(misc_device->name);
	ida_simple_remove(&pci_endpoint_test_ida, id);
	for (bar = BAR_0; bar <= BAR_5; bar++) {
		if (test->bar[bar])
			pci_iounmap(pdev, test->bar[bar]);
	}

	pci_endpoint_test_release_irq(test);
	pci_endpoint_test_free_irq_vectors(test);

	pci_release_regions(pdev);
	pci_disable_device(pdev);
#endif
	sc_pci_endpoint_serial_remove(pdev);
}

static const struct pci_device_id sc_pci_pub_tbl[] = {
	{ PCI_DEVICE(0x1cf2, 0xabce) },
//	{ PCI_DEVICE(0x1234, 0x1234) },
	{ }
};
MODULE_DEVICE_TABLE(pci, sc_pci_pub_tbl);

static struct pci_driver sc_pci_pub_driver = {
	.name		= DRV_MODULE_NAME,
	.id_table	= sc_pci_pub_tbl,
	.probe		= sc_pci_pub_probe,
	.remove		= sc_pci_pub_remove,
};
module_pci_driver(sc_pci_pub_driver);

MODULE_DESCRIPTION("PCI ENDPOINT TEST HOST DRIVER");
MODULE_AUTHOR("Kishon Vijay Abraham I <kishon@ti.com>");
MODULE_LICENSE("GPL v2");


