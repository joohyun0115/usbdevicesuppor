/*
 * Gadget Function Driver for Android USB kgdb
 *
 * This file is based in part on 
 * 		f_adb in Android Linux Kernel
 *
 * Copyright (C) 2011 Sevencore, Inc.
 * Author: Joohyun Kyong <joohyun0115@gmail.com>
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

/* #define DEBUG */
/* #define VERBOSE_DEBUG */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/poll.h>
#include <linux/delay.h>
#include <linux/wait.h>
#include <linux/err.h>
#include <linux/interrupt.h>
#include <linux/kthread.h>
#include <linux/freezer.h>

#include <linux/types.h>
#include <linux/file.h>
#include <linux/device.h>
#include <linux/miscdevice.h>

#include <linux/usb.h>
#include <linux/usb/ch9.h>
#include <linux/usb/android_composite.h>


#define BULK_BUFFER_SIZE    16384
#define KGDB_STRING_SIZE     256

#define PROTOCOL_VERSION    1

/* String IDs */
#define INTERFACE_STRING_INDEX	0

/* number of tx and rx requests to allocate */
#define TX_REQ_MAX 4
#define RX_REQ_MAX 2

struct kgdb_dev {
	struct usb_function function;
	struct usb_composite_dev *cdev;
	spinlock_t lock;

	struct usb_ep *ep_in;
	struct usb_ep *ep_out;

	/* set to 1 when we connect */
	int online:1;
	/* Set to 1 when we disconnect.
	 * Not cleared until our file is closed.
	 */
	int disconnected:1;
	struct list_head tx_idle;

	wait_queue_head_t read_wq;
	wait_queue_head_t write_wq;
	struct usb_request *rx_req[RX_REQ_MAX];
	int rx_done;
};

static struct usb_interface_descriptor kgdb_interface_desc = {
	.bLength                = USB_DT_INTERFACE_SIZE,
	.bDescriptorType        = USB_DT_INTERFACE,
	.bInterfaceNumber       = 0,
	.bNumEndpoints          = 2,
	.bInterfaceClass        = 0xff,
	.bInterfaceSubClass     = 0x50,
	.bInterfaceProtocol     = 1,
};

static struct usb_endpoint_descriptor kgdb_highspeed_in_desc = {
	.bLength                = USB_DT_ENDPOINT_SIZE,
	.bDescriptorType        = USB_DT_ENDPOINT,
	.bEndpointAddress       = USB_DIR_IN,
	.bmAttributes           = USB_ENDPOINT_XFER_BULK,
	.wMaxPacketSize         = __constant_cpu_to_le16(512),
};

static struct usb_endpoint_descriptor kgdb_highspeed_out_desc = {
	.bLength                = USB_DT_ENDPOINT_SIZE,
	.bDescriptorType        = USB_DT_ENDPOINT,
	.bEndpointAddress       = USB_DIR_OUT,
	.bmAttributes           = USB_ENDPOINT_XFER_BULK,
	.wMaxPacketSize         = __constant_cpu_to_le16(512),
};

static struct usb_endpoint_descriptor kgdb_fullspeed_in_desc = {
	.bLength                = USB_DT_ENDPOINT_SIZE,
	.bDescriptorType        = USB_DT_ENDPOINT,
	.bEndpointAddress       = USB_DIR_IN,
	.bmAttributes           = USB_ENDPOINT_XFER_BULK,
};

static struct usb_endpoint_descriptor kgdb_fullspeed_out_desc = {
	.bLength                = USB_DT_ENDPOINT_SIZE,
	.bDescriptorType        = USB_DT_ENDPOINT,
	.bEndpointAddress       = USB_DIR_OUT,
	.bmAttributes           = USB_ENDPOINT_XFER_BULK,
};

static struct usb_descriptor_header *fs_kgdb_descs[] = {
	(struct usb_descriptor_header *) &kgdb_interface_desc,
	(struct usb_descriptor_header *) &kgdb_fullspeed_in_desc,
	(struct usb_descriptor_header *) &kgdb_fullspeed_out_desc,
	NULL,
};

static struct usb_descriptor_header *hs_kgdb_descs[] = {
	(struct usb_descriptor_header *) &kgdb_interface_desc,
	(struct usb_descriptor_header *) &kgdb_highspeed_in_desc,
	(struct usb_descriptor_header *) &kgdb_highspeed_out_desc,
	NULL,
};

static struct usb_string kgdb_string_defs[] = {
	[INTERFACE_STRING_INDEX].s	= "Android KGDB Interface",
	{  },	/* end of list */
};

static struct usb_gadget_strings kgdb_string_table = {
	.language		= 0x0409,	/* en-US */
	.strings		= kgdb_string_defs,
};

static struct usb_gadget_strings *kgdb_strings[] = {
	&kgdb_string_table,
	NULL,
};


/* temporary variable used between kgdb_open() and kgdb_gadget_bind() */
static struct kgdb_dev *_kgdb_dev;

static inline struct kgdb_dev *func_to_dev(struct usb_function *f)
{
	return container_of(f, struct kgdb_dev, function);
}

static struct usb_request *kgdb_request_new(struct usb_ep *ep, int buffer_size)
{
	struct usb_request *req = usb_ep_alloc_request(ep, GFP_KERNEL);
	if (!req)
		return NULL;

	/* now allocate buffers for the requests */
	req->buf = kmalloc(buffer_size, GFP_KERNEL);
	if (!req->buf) {
		usb_ep_free_request(ep, req);
		return NULL;
	}

	return req;
}

static void kgdb_request_free(struct usb_request *req, struct usb_ep *ep)
{
	if (req) {
		kfree(req->buf);
		usb_ep_free_request(ep, req);
	}
}

/* add a request to the tail of a list */
static void req_put(struct kgdb_dev *dev, struct list_head *head,
		struct usb_request *req)
{
	unsigned long flags;

	spin_lock_irqsave(&dev->lock, flags);
	list_add_tail(&req->list, head);
	spin_unlock_irqrestore(&dev->lock, flags);
}

/* remove a request from the head of a list */
static struct usb_request *req_get(struct kgdb_dev *dev, struct list_head *head)
{
	unsigned long flags;
	struct usb_request *req;

	spin_lock_irqsave(&dev->lock, flags);
	if (list_empty(head)) {
		req = 0;
	} else {
		req = list_first_entry(head, struct usb_request, list);
		list_del(&req->list);
	}
	spin_unlock_irqrestore(&dev->lock, flags);
	return req;
}

static void kgdb_set_disconnected(struct kgdb_dev *dev)
{
	dev->online = 0;
	dev->disconnected = 1;
}

static void kgdb_complete_in(struct usb_ep *ep, struct usb_request *req)
{
	struct kgdb_dev *dev = _kgdb_dev;

	if (req->status != 0)
		kgdb_set_disconnected(dev);

	req_put(dev, &dev->tx_idle, req);

	wake_up(&dev->write_wq);
}

static void kgdb_complete_out(struct usb_ep *ep, struct usb_request *req)
{
	struct kgdb_dev *dev = _kgdb_dev;

	dev->rx_done = 1;
	if (req->status != 0)
		kgdb_set_disconnected(dev);
	wake_up(&dev->read_wq);
}

static int __init create_bulk_endpoints(struct kgdb_dev *dev,
				struct usb_endpoint_descriptor *in_desc,
				struct usb_endpoint_descriptor *out_desc)
{
	struct usb_composite_dev *cdev = dev->cdev;
	struct usb_request *req;
	struct usb_ep *ep;
	int i;

	DBG(cdev, "create_bulk_endpoints dev: %p\n", dev);

	ep = usb_ep_autoconfig(cdev->gadget, in_desc);
	if (!ep) {
		DBG(cdev, "usb_ep_autoconfig for ep_in failed\n");
		return -ENODEV;
	}
	DBG(cdev, "usb_ep_autoconfig for ep_in got %s\n", ep->name);
	ep->driver_data = dev;		/* claim the endpoint */
	dev->ep_in = ep;

	ep = usb_ep_autoconfig(cdev->gadget, out_desc);
	if (!ep) {
		DBG(cdev, "usb_ep_autoconfig for ep_out failed\n");
		return -ENODEV;
	}
	DBG(cdev, "usb_ep_autoconfig for ep_out got %s\n", ep->name);
	ep->driver_data = dev;		/* claim the endpoint */
	dev->ep_out = ep;

	ep = usb_ep_autoconfig(cdev->gadget, out_desc);
	if (!ep) {
		DBG(cdev, "usb_ep_autoconfig for ep_out failed\n");
		return -ENODEV;
	}
	DBG(cdev, "usb_ep_autoconfig for ep_out got %s\n", ep->name);
	ep->driver_data = dev;		/* claim the endpoint */
	dev->ep_out = ep;

	/* now allocate requests for our endpoints */
	for (i = 0; i < TX_REQ_MAX; i++) {
		req = kgdb_request_new(dev->ep_in, BULK_BUFFER_SIZE);
		if (!req)
			goto fail;
		req->complete = kgdb_complete_in;
		req_put(dev, &dev->tx_idle, req);
	}
	for (i = 0; i < RX_REQ_MAX; i++) {
		req = kgdb_request_new(dev->ep_out, BULK_BUFFER_SIZE);
		if (!req)
			goto fail;
		req->complete = kgdb_complete_out;
		dev->rx_req[i] = req;
	}

	return 0;

fail:
	printk(KERN_ERR "kgdb_bind() could not allocate requests\n");
	while ((req = req_get(dev, &dev->tx_idle)))
		kgdb_request_free(req, dev->ep_in);
	for (i = 0; i < RX_REQ_MAX; i++)
		kgdb_request_free(dev->rx_req[i], dev->ep_out);
	return -1;
}

int platform_usb_handler(void);

ssize_t kgdb_read(char  *buf, size_t count)
{
	struct kgdb_dev *dev = _kgdb_dev;
	struct usb_composite_dev *cdev = dev->cdev;
	struct usb_request *req;
	int r = count, xfer;
	int ret = 0;

	DBG(cdev, "kgdb_read(%d)\n", count);

	if (count > BULK_BUFFER_SIZE)
		count = BULK_BUFFER_SIZE;

	/* we will block until we're online */
	DBG(cdev, "kgdb_read: waiting for online\n");

	/* we will block until we're online */
	while (!(dev->online)) {
		DBG(cdev, "kgdb_read: waiting for online state\n");
		platform_usb_handler();
	}
	

requeue_req:
	/* queue a request */
	req = dev->rx_req[0];
	req->length = count;
	dev->rx_done = 0;
	ret = usb_ep_queue(dev->ep_out, req, GFP_KERNEL);
	
	if (ret < 0) {
		r = -EIO;
		goto done;
	} 
	
	while (!dev->rx_done) {
		platform_usb_handler();
	}
	
	if (dev->online) {
		/* If we got a 0-len packet, throw it back and try again. */
		if (req->actual == 0)
			goto requeue_req;

		DBG(cdev, "rx %p %d\n", req, req->actual);
		xfer = (req->actual < count) ? req->actual : count;
		r = xfer;
		memcpy(buf, req->buf, xfer);

	} else
		r = -EIO;

done:
	DBG(cdev, "kgdb_read returning %d\n", r);
	return r;
}


ssize_t kgdb_write(char  *buf, size_t count)
{
	struct kgdb_dev *dev = _kgdb_dev;
	struct usb_composite_dev *cdev = dev->cdev;
	struct usb_request *req = 0;
	int r = count, xfer;
	int ret;

	if (!dev->online || dev->disconnected)
		return -ENODEV;

	while (count > 0) {
		if (!dev->online) {
			printk("kgdb_write dev->error\n");
			r = -EIO;
			break;
		}
		
		/* get an idle tx request to use */
		req = 0;

		while ( !(req = req_get(dev, &dev->tx_idle))) {
			platform_usb_handler(); 
		}		
		if (!req) {
			r = ret;
			break;
		}

		if (count > BULK_BUFFER_SIZE)
			xfer = BULK_BUFFER_SIZE;
		else
			xfer = count;

		memcpy(req->buf, buf, xfer);
		
		req->length = xfer;
		ret = usb_ep_queue(dev->ep_in, req, GFP_KERNEL);
		if (ret < 0) {
			DBG(cdev, "kgdb_write: xfer error %d\n", ret);
			r = -EIO;
			break;
		}


		buf += xfer;
		count -= xfer;

		/* zero this so we don't try to free it on error exit */
		req = 0;
		
	}
	
	if (req)
		req_put(dev, &dev->tx_idle, req);

	DBG(cdev, "kgdb_write returning %d\n", r);
	return r;
}


static int
kgdb_function_bind(struct usb_configuration *c, struct usb_function *f)
{
	struct usb_composite_dev *cdev = c->cdev;
	struct kgdb_dev	*dev = func_to_dev(f);
	int			id;
	int			ret;

	dev->cdev = cdev;
	DBG(cdev, "kgdb_function_bind dev: %p\n", dev);

	/* allocate interface ID(s) */
	id = usb_interface_id(c, f);
	if (id < 0)
		return id;
	kgdb_interface_desc.bInterfaceNumber = id;

	/* allocate endpoints */
	ret = create_bulk_endpoints(dev, &kgdb_fullspeed_in_desc,
			&kgdb_fullspeed_out_desc);
	if (ret)
		return ret;

	/* support high speed hardware */
	if (gadget_is_dualspeed(c->cdev->gadget)) {
		kgdb_highspeed_in_desc.bEndpointAddress =
			kgdb_fullspeed_in_desc.bEndpointAddress;
		kgdb_highspeed_out_desc.bEndpointAddress =
			kgdb_fullspeed_out_desc.bEndpointAddress;
	}

	DBG(cdev, "%s speed %s: IN/%s, OUT/%s\n",
			gadget_is_dualspeed(c->cdev->gadget) ? "dual" : "full",
			f->name, dev->ep_in->name, dev->ep_out->name);
	return 0;
}

static void
kgdb_function_unbind(struct usb_configuration *c, struct usb_function *f)
{
	struct kgdb_dev	*dev = func_to_dev(f);
	struct usb_request *req;
	int i;

	spin_lock_irq(&dev->lock);
	while ((req = req_get(dev, &dev->tx_idle)))
		kgdb_request_free(req, dev->ep_in);
	for (i = 0; i < RX_REQ_MAX; i++)
		kgdb_request_free(dev->rx_req[i], dev->ep_out);
	dev->online = 0;
	spin_unlock_irq(&dev->lock);

	
	kfree(_kgdb_dev);
	_kgdb_dev = NULL;
}

static int kgdb_function_set_alt(struct usb_function *f,
		unsigned intf, unsigned alt)
{
	struct kgdb_dev	*dev = func_to_dev(f);
	struct usb_composite_dev *cdev = f->config->cdev;
	int ret;

	DBG(cdev, "kgdb_function_set_alt intf: %d alt: %d\n", intf, alt);
	ret = usb_ep_enable(dev->ep_in,
			ep_choose(cdev->gadget,
				&kgdb_highspeed_in_desc,
				&kgdb_fullspeed_in_desc));
	if (ret)
		return ret;
	ret = usb_ep_enable(dev->ep_out,
			ep_choose(cdev->gadget,
				&kgdb_highspeed_out_desc,
				&kgdb_fullspeed_out_desc));
	if (ret) {
		usb_ep_disable(dev->ep_in);
		return ret;
	}
	if (!dev->function.disabled)
		dev->online = 1;

	/* readers may be blocked waiting for us to go online */
	wake_up(&dev->read_wq);
	return 0;
}

static void kgdb_function_disable(struct usb_function *f)
{
	struct kgdb_dev	*dev = func_to_dev(f);
	struct usb_composite_dev	*cdev = dev->cdev;

	DBG(cdev, "kgdb_function_disable\n");
	kgdb_set_disconnected(dev);
	usb_ep_disable(dev->ep_in);
	usb_ep_disable(dev->ep_out);

	/* readers may be blocked waiting for us to go online */
	wake_up(&dev->read_wq);

	VDBG(cdev, "%s disabled\n", dev->function.name);
}


#include <linux/kgdb.h>

static void breakpoint_func(struct work_struct *work)
{
	printk(KERN_CRIT "kgdb: Waiting for USB kgdb connection from remote gdb...\n");
	kgdb_schedule_breakpoint();
}

static DECLARE_DELAYED_WORK(breakpoint_work, breakpoint_func);


static int kgdb_bind_config(struct usb_configuration *c)
{
	struct kgdb_dev *dev;
	int ret;

	printk(KERN_INFO "kgdb_bind_config\n");

	dev = kzalloc(sizeof(*dev), GFP_KERNEL);
	if (!dev)
		return -ENOMEM;

	/* allocate a string ID for our interface */
	if (kgdb_string_defs[INTERFACE_STRING_INDEX].id == 0) {
		ret = usb_string_id(c->cdev);
		if (ret < 0)
			return ret;
		kgdb_string_defs[INTERFACE_STRING_INDEX].id = ret;
		kgdb_interface_desc.iInterface = ret;
	}

	spin_lock_init(&dev->lock);
	init_waitqueue_head(&dev->read_wq);
	init_waitqueue_head(&dev->write_wq);
	INIT_LIST_HEAD(&dev->tx_idle);

	dev->cdev = c->cdev;
	dev->function.name = "kgdb";
	dev->function.strings = kgdb_strings,
	dev->function.descriptors = fs_kgdb_descs;
	dev->function.hs_descriptors = hs_kgdb_descs;
	dev->function.bind = kgdb_function_bind;
	dev->function.unbind = kgdb_function_unbind;
	dev->function.set_alt = kgdb_function_set_alt;
	dev->function.disable = kgdb_function_disable;
	dev->function.disabled = 1;

	/* _kgdb_dev must be set before calling usb_gadget_register_driver */
	_kgdb_dev = dev;

	ret = usb_add_function(c, &dev->function);
	if (ret)
		goto err1;

	android_enable_function(&dev->function, 1);
	
	INIT_DELAYED_WORK(&breakpoint_work, breakpoint_func);
	schedule_delayed_work(&breakpoint_work, msecs_to_jiffies(100));
	
	return 0;
err1:
	kfree(dev);
	printk(KERN_ERR "USB kgdb gadget driver failed to initialize\n");
	return ret;
}

static struct android_usb_function kgdb_function = {
	.name = "kgdb",
	.bind_config = kgdb_bind_config,
};

static int __init init(void)
{
	printk(KERN_INFO "f_kgdb init\n");
	android_register_function(&kgdb_function);

	return 0;
}
module_init(init);

