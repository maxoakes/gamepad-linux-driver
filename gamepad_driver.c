// SPDX-License-Identifier: GPL-2.0
/*
 * USB Skeleton driver - 2.2
 *
 * Copyright (C) 2001-2004 Greg Kroah-Hartman (greg@kroah.com)
 *
 * This driver is based on the 2.6.3 version of drivers/usb/usb-skeleton.c
 * but has been rewritten to be easier to read and use.
 */

#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/kref.h>
#include <linux/uaccess.h>
#include <linux/usb.h>
#include <linux/mutex.h>
#include <linux/input.h>
#include <linux/stat.h>
#include <linux/usb/input.h>
#include <linux/rcupdate.h>

// T:  Bus=03 Lev=01 Prnt=01 Port=01 Cnt=01 Dev#=  7 Spd=12  MxCh= 0
// D:  Ver= 2.00 Cls=ff(vend.) Sub=ff Prot=ff MxPS= 8 #Cfgs=  1
// P:  Vendor=046d ProdID=c21d Rev=40.14
// S:  Manufacturer=Logitech
// S:  Product=Gamepad F310
// S:  SerialNumber=E10C8732
// C:  #Ifs= 1 Cfg#= 1 Atr=80 MxPwr=500mA
// I:  If#=0x0 Alt= 0 #EPs= 2 Cls=ff(vend.) Sub=5d Prot=01 Driver=gamepad
#define USB_GAMEPAD_VENDOR_ID	0x046d
#define USB_GAMEPAD_PRODUCT_ID	0xc21d
#define USB_GAMEPAD_NAME "Gamepad F310"

//data[3]
#define BUTTON_A 0x10
#define BUTTON_B 0x20
#define BUTTON_X 0x40
#define BUTTON_Y 0x80

//data[2]
#define BUTTON_UP 		0x01
#define BUTTON_DOWN 	0x02
#define BUTTON_LEFT 	0x04
#define BUTTON_RIGHT	0x08

//data[2]
#define BUTTON_SELECT 	0x20
#define BUTTON_START	0x10
#define BUTTON_MODE		0x04

//data[3]
#define BUTTON_L1 	0x01
#define BUTTON_R1	0x02

//data[2]
#define BUTTON_L3 	0x40
#define BUTTON_R3	0x80

#define GAMEPAD_PKT_LEN 64
#define USB_GAMEPAD_MINOR_BASE	192
#define MAX_TRANSFER (PAGE_SIZE - 512)
#define WRITES_IN_FLIGHT 8

/* table of devices that work with this driver */
static const struct usb_device_id gamepad_table[] = {
	{ USB_DEVICE(USB_GAMEPAD_VENDOR_ID, USB_GAMEPAD_PRODUCT_ID) },
	{ }					/* Terminating entry */
};
MODULE_DEVICE_TABLE(usb, gamepad_table);

static const signed short gamepad_buttons[] = {
	BTN_A, BTN_B, BTN_X, BTN_Y,
	BTN_START, BTN_SELECT, BTN_MODE,
	BTN_THUMBL, BTN_THUMBR,
	BTN_TL, BTN_TR,	
	-1 };

static const signed short directional_buttons[] = {
	BTN_TRIGGER_HAPPY1, BTN_TRIGGER_HAPPY2,		/* d-pad left, right */
	BTN_TRIGGER_HAPPY3, BTN_TRIGGER_HAPPY4,		/* d-pad up, down */
	-1 };

static const signed short trigger_buttons[] = {
	BTN_TL2, BTN_TR2,		/* triggers left/right */
	-1 };

static const signed short trigger_bumpers[] = {
	ABS_Z, ABS_RZ,		/* triggers left/right */
	-1 };

static const signed short gamepad_abs[] = {
	ABS_X, ABS_Y,		/* left stick */
	ABS_RX, ABS_RY,		/* right stick */
	-1 };

struct usb_gamepad {
	const char 				*name;		/* name of the device */
	char 					phys[64];
	struct usb_device		*udev;		/* the usb device for this device */
	struct input_dev		*idev;		/* the input device */
	struct usb_interface	*interface;	/* the interface for this device */
	struct semaphore		limit_sem;	/* limiting the number of writes in progress */
	struct usb_anchor	submitted;		/* in case we need to retract our submissions */
	spinlock_t		err_lock;		/* lock for errors */
	struct kref		kref;
	struct mutex	io_mutex;		/* synchronize I/O with disconnect */
	struct input_dev __rcu *x360w_dev;

	struct work_struct work;	/* init/remove device from callback */
	bool pad_present;
	bool input_created;

	//out urb? why?
	struct urb *int_out_urb;		/* urb for interrupt out report */
	struct usb_anchor int_out_anchor;
	bool irq_out_active;		/* we must not use an active URB */
	unsigned char *odata;		/* output data */
	dma_addr_t odata_dma;
	spinlock_t odata_lock;

	//urb
	struct urb		*int_in_urb;	/* the urb to read data with */
	size_t			int_in_size;		/* the size of the receive buffer */
	size_t			int_in_filled;		/* number of bytes in the buffer */
	size_t			int_in_copied;		/* already copied to user space */
	__u8			int_in_endpointAddr;
	__u8			int_out_endpointAddr;
	wait_queue_head_t	int_in_wait;
	unsigned char *idata;		/* input data */
	dma_addr_t idata_dma;
};

#define to_gamepad_dev(d) container_of(d, struct usb_gamepad, kref)

static struct usb_driver gamepad_driver;

static void gamepad_delete(struct kref *kref)
{
	printk(KERN_ALERT "gamepad_delete\n");
	struct usb_gamepad *dev = to_gamepad_dev(kref);

	usb_free_urb(dev->int_in_urb);
	printk(KERN_ALERT "urb freed\n");
	usb_put_intf(dev->interface);
	printk(KERN_ALERT "interface cleared\n");
	usb_put_dev(dev->udev);
	printk(KERN_ALERT "usb dev cleared\n");
	//kfree(dev->idata);
	//printk(KERN_ALERT "idata freed\n");
	kfree(dev);
	printk(KERN_ALERT "usb_gamepad freed\n");
}

static int gamepad_open(struct inode *inode, struct file *file)
{
	printk(KERN_ALERT "gamepad_open\n");
	struct usb_gamepad *dev;
	struct usb_interface *interface;
	int subminor;
	int retval = 0;

	subminor = iminor(inode);

	interface = usb_find_interface(&gamepad_driver, subminor);
	if (!interface) {
		pr_err("%s - error, can't find device for minor %d\n",	__func__, subminor);
		retval = -ENODEV;
		goto exit;
	}

	dev = usb_get_intfdata(interface);
	if (!dev) {
		retval = -ENODEV;
		goto exit;
	}

	retval = usb_autopm_get_interface(interface);
	if (retval)
		goto exit;

	/* increment our usage count for the device */
	kref_get(&dev->kref);

	/* save our object in the file's private structure */
	file->private_data = dev;

exit:
	return retval;
}

static int usb_gamepad_open(struct input_dev *dev)
{
	printk(KERN_ALERT "usb_gamepad_open");
	struct usb_gamepad *gamepad = input_get_drvdata(dev);

	if (usb_submit_urb(gamepad->int_in_urb, GFP_KERNEL))
		return -EIO;

	return 0;
}

static void usb_gamepad_close(struct input_dev *dev)
{
	printk(KERN_ALERT "usb_gamepad_close");
	struct usb_gamepad *gamepad = input_get_drvdata(dev);
	usb_kill_urb(gamepad->int_in_urb);
}

static void gamepad_irq_out(struct urb *urb)
{
	struct usb_gamepad *gamepad = urb->context;
	struct device *dev = &gamepad->interface->dev;
	int status = urb->status;
	int error;
	unsigned long flags;

	spin_lock_irqsave(&gamepad->odata_lock, flags);

	switch (status) {
	case 0:
		/* success */
		break;

	case -ECONNRESET:
	case -ENOENT:
	case -ESHUTDOWN:
		/* this urb is terminated, clean up */
		dev_dbg(dev, "%s - urb shutting down with status: %d\n",
			__func__, status);
		gamepad->irq_out_active = false;
		break;

	default:
		dev_dbg(dev, "%s - nonzero urb status received: %d\n",
			__func__, status);
		break;
	}

	spin_unlock_irqrestore(&gamepad->odata_lock, flags);
}

static int gamepad_init_output(struct usb_interface *interface, struct usb_gamepad *gamepad, struct usb_endpoint_descriptor *ep_irq_out)
{
	int error;

	init_usb_anchor(&gamepad->int_out_anchor);

	gamepad->odata = usb_alloc_coherent(gamepad->udev, GAMEPAD_PKT_LEN, GFP_KERNEL, &gamepad->odata_dma);
	if (!gamepad->odata)
		return -ENOMEM;

	spin_lock_init(&gamepad->odata_lock);

	gamepad->int_out_urb = usb_alloc_urb(0, GFP_KERNEL);
	if (!gamepad->int_out_urb) {
		error = -ENOMEM;
		goto err_free_coherent;
	}

	usb_fill_int_urb(gamepad->int_out_urb, gamepad->udev,
			 usb_sndintpipe(gamepad->udev, ep_irq_out->bEndpointAddress),
			 gamepad->odata, GAMEPAD_PKT_LEN,
			 gamepad_irq_out, gamepad, ep_irq_out->bInterval);
	gamepad->int_out_urb->transfer_dma = gamepad->odata_dma;
	gamepad->int_out_urb->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;

	return 0;

err_free_coherent:
	usb_free_coherent(gamepad->udev, GAMEPAD_PKT_LEN, gamepad->odata, gamepad->odata_dma);
	return error;
}

static void gamepad_read_int_callback(struct urb *urb)
{
	unsigned char *data = urb->transfer_buffer;
	struct usb_gamepad *gamepad = urb->context;
	struct input_dev *dev = gamepad->idev;
	int retval;

	switch (urb->status) {
	case 0:
		/* success */
		break;
	case -ECONNRESET:
	case -ENOENT:
	case -ESHUTDOWN:
		/* this urb is terminated, clean up */
		printk("%s - urb shutting down with status: %d", __func__, urb->status);
		return;
	default:
		printk("%s - nonzero urb status received: %d", __func__, urb->status);
		goto exit;
	}

	printk("");
	int i;
	printk(KERN_CONT "array cells: ");
	for (i = 0; i < urb->actual_length; ++i)
		printk(KERN_CONT " %i:%02x ", i, data[i]);
	printk("");

	// printk(KERN_CONT "memory: ");
	// for (i = 0; i < urb->actual_length; ++i)
	// 	printk(KERN_CONT " %i:%02x ", i, (__s16) le16_to_cpup((__le16 *)(data + i)));
	// printk("");

	input_report_key(dev, BTN_TRIGGER_HAPPY1, data[2] & BUTTON_LEFT);
	input_report_key(dev, BTN_TRIGGER_HAPPY2, data[2] & BUTTON_RIGHT);
	input_report_key(dev, BTN_TRIGGER_HAPPY3, data[2] & BUTTON_UP);
	input_report_key(dev, BTN_TRIGGER_HAPPY4, data[2] & BUTTON_DOWN);

	input_report_key(dev, BTN_A,  data[3] & BUTTON_A);
	input_report_key(dev, BTN_B,  data[3] & BUTTON_B);
	input_report_key(dev, BTN_X,  data[3] & BUTTON_X);
	input_report_key(dev, BTN_Y,  data[3] & BUTTON_Y);
	input_report_key(dev, BTN_TL, data[3] & BUTTON_L1);
	input_report_key(dev, BTN_TR, data[3] & BUTTON_R1);
	
	/* start/back buttons */
	input_report_key(dev, BTN_START,  data[2] & BUTTON_START);
	input_report_key(dev, BTN_SELECT, data[2] & BUTTON_SELECT);
	input_report_key(dev, BTN_MODE, data[3] & BUTTON_MODE);

	/* stick press left/right */
	input_report_key(dev, BTN_THUMBL, data[2] & BUTTON_L3);
	input_report_key(dev, BTN_THUMBR, data[2] & BUTTON_R3);

	//digital bumpers
	// input_report_key(dev, BTN_TL2, data[4]);
	// input_report_key(dev, BTN_TR2, data[5]);

	//analog bumpers
	input_report_abs(dev, ABS_Z, data[4]);
	input_report_abs(dev, ABS_RZ, data[5]);

	/* left stick */
	input_report_abs(dev, ABS_X, (__s16) le16_to_cpup((__le16 *)(data + 6)));
	input_report_abs(dev, ABS_Y, ~(__s16) le16_to_cpup((__le16 *)(data + 8)));

	/* right stick */
	input_report_abs(dev, ABS_RX, (__s16) le16_to_cpup((__le16 *)(data + 10)));
	input_report_abs(dev, ABS_RY, ~(__s16) le16_to_cpup((__le16 *)(data + 12)));

	input_sync(dev);

exit:
	retval = usb_submit_urb(urb, GFP_ATOMIC);
	if (retval)
		dev_err(&urb->dev->dev, "%s - Error %d submitting interrupt urb\n", __func__, retval);
}

static void gamepad_deinit_output(struct usb_gamepad *gamepad)
{
	usb_free_urb(gamepad->int_out_urb);
	usb_free_coherent(gamepad->udev, GAMEPAD_PKT_LEN, gamepad->odata, gamepad->odata_dma);
}

static void gamepad_deinit_input(struct usb_gamepad *gamepad)
{
	if (gamepad->input_created) {
		gamepad->input_created = false;
		input_unregister_device(gamepad->idev);
	}
}

static void gamepad_set_up_abs(struct input_dev *input_dev, signed short abs)
{
	struct usb_gamepad *gamepad = input_get_drvdata(input_dev);

	switch (abs) {
	case ABS_X:
	case ABS_Y:
	case ABS_RX:
	case ABS_RY:	/* the two sticks */
		input_set_abs_params(input_dev, abs, -32768, 32767, 16, 128);
		break;
	case ABS_Z:
	case ABS_RZ:	/* the triggers (if mapped to axes) */
		input_set_abs_params(input_dev, abs, 0, 255, 0, 0);
		break;
	case ABS_HAT0X:
	case ABS_HAT0Y:	/* the d-pad (only if dpad is mapped to axes */
		input_set_abs_params(input_dev, abs, -1, 1, 0, 0);
		break;
	default:
		input_set_abs_params(input_dev, abs, 0, 0, 0, 0);
		break;
	}
}

static int gamepad_init_input(struct usb_gamepad *dev)
{
	struct input_dev *input_dev;
	int i, error;

	input_dev = input_allocate_device();
	if (!input_dev) return -ENOMEM;

	dev->idev = input_dev;
	input_dev->name = dev->name;
	input_dev->phys = dev->phys;
	usb_to_input_id(dev->udev, &input_dev->id);

	input_dev->dev.parent = &dev->interface->dev;
	input_set_drvdata(input_dev, dev);

	input_dev->open = usb_gamepad_open;
	input_dev->close = usb_gamepad_close;

	for (i = 0; gamepad_buttons[i] >= 0; i++)
		input_set_capability(input_dev, EV_KEY, gamepad_buttons[i]);

	for (i = 0; directional_buttons[i] >= 0; i++)
		input_set_capability(input_dev, EV_KEY, directional_buttons[i]);

	// for (i = 0; trigger_buttons[i] >= 0; i++)
	// 	input_set_capability(input_dev, EV_KEY, trigger_buttons[i]);

	for (i = 0; trigger_bumpers[i] >= 0; i++)
			gamepad_set_up_abs(input_dev, trigger_bumpers[i]);

	for (i = 0; gamepad_abs[i] >= 0; i++)
		gamepad_set_up_abs(input_dev, gamepad_abs[i]);

	/* we can register the device now, as it is ready */
	error = input_register_device(dev->idev);
	if (error) {
		/* something prevented us from registering this driver */
		dev_err(&dev->interface->dev, "error with input_register_device.\n");
	}

	dev->input_created = true;
	return 0;
}

static void gamepad_presence_work(struct work_struct *work)
{
	printk(KERN_ALERT "gamepad_presence_work\n");
	struct usb_gamepad *gamepad = container_of(work, struct usb_gamepad, work);
	int error;

	if (gamepad->pad_present) {
		error = gamepad_init_input(gamepad);
		if (error) {
			/* complain only, not much else we can do here */
			dev_err(&gamepad->idev->dev, "unable to init device: %d\n", error);
		} else {
			rcu_assign_pointer(gamepad->x360w_dev, gamepad->idev);
		}
	} else {
		printk(KERN_ALERT "RCU?!?!?!!\n");
		RCU_INIT_POINTER(gamepad->x360w_dev, NULL);
		synchronize_rcu();
		/*
		 * Now that we are sure xpad360w_process_packet is not
		 * using input device we can get rid of it.
		 */
		gamepad_deinit_input(gamepad);
	}
}

/*
	Probe
	
	The probe function is called when a device is installed that the USB core thinks this driver should handle;
	the probe function should perform checks on the information passed to it about the device and decide whether
	the driver is really appropriate for that device.
*/
static int gamepad_probe(struct usb_interface *interface, const struct usb_device_id *id)
{
	printk(KERN_ALERT "gamepad_probe\n");
	struct usb_gamepad *dev;
	struct usb_device *udev = interface_to_usbdev(interface);
	struct usb_endpoint_descriptor *int_in, *int_out;
	struct input_dev *input_dev;
	int retval;

	//create the device
	dev = kzalloc(sizeof(*dev), GFP_KERNEL);
	if (!dev) return -ENOMEM;

	//init struct primitives
	kref_init(&dev->kref);
	sema_init(&dev->limit_sem, WRITES_IN_FLIGHT);
	mutex_init(&dev->io_mutex);
	spin_lock_init(&dev->err_lock);
	init_usb_anchor(&dev->submitted);
	init_waitqueue_head(&dev->int_in_wait);

	//assign structs to usb_gamepad
	dev->udev = udev;
	dev->interface = interface;

	//get the physical path and name of device
	usb_make_path(dev->udev, dev->phys, sizeof(dev->phys));
	strlcat(dev->phys, "/input0", sizeof(dev->phys));
	dev->name = USB_GAMEPAD_NAME;

	//allocate the memory that the input data will go into
	dev->idata = usb_alloc_coherent(dev->udev, GAMEPAD_PKT_LEN, GFP_KERNEL, &dev->idata_dma);

	dev->int_in_urb = usb_alloc_urb(0, GFP_KERNEL);
	if (!dev->int_in_urb) {
		retval = -ENOMEM;
		goto error;
	}

	//INIT_WORK(&dev->work, gamepad_presence_work);

	/* set up the endpoint information */
	/* use only the first interupt-in and interupt-out endpoints */
	// retval = usb_find_common_endpoints(interface->cur_altsetting, NULL, NULL, &int_in, &int_out);
	// if (retval) {
	// 	dev_err(&interface->dev, "Could not find both interupt-in and interupt-out endpoints\n");
	// 	goto error;
	// }

	int_in = int_out = NULL;

	int i;
	for (i = 0; i < 2; i++) {
		struct usb_endpoint_descriptor *ep = &interface->cur_altsetting->endpoint[i].desc;

		if (usb_endpoint_xfer_int(ep))
		{
			if (usb_endpoint_dir_in(ep))
				int_in = ep;
			else
				int_out = ep;
		}
	}

	if (!int_in || !int_out) {
		printk(KERN_ALERT "error in urb !int_in || !int_out\n");
		goto error;
	}

	retval = gamepad_init_output(interface, dev, int_out);
	if (retval) goto error;

	//dev->int_in_size = usb_endpoint_maxp(int_in);
	//dev->int_in_endpointAddr = int_in->bEndpointAddress;
	//dev->int_out_endpointAddr = int_out->bEndpointAddress;
	// dev->idata = kmalloc(dev->int_in_size, GFP_KERNEL);
	// if (!dev->idata) {
	// 	retval = -ENOMEM;
	// 	goto error;
	// }	

	usb_fill_int_urb(dev->int_in_urb,
		dev->udev,
		usb_rcvintpipe(dev->udev, int_in->bEndpointAddress),
		dev->idata,
		GAMEPAD_PKT_LEN,
		gamepad_read_int_callback,
		dev,
		int_in->bInterval);
	dev->int_in_urb->transfer_dma = dev->idata_dma;
	dev->int_in_urb->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;

	usb_set_intfdata(interface, dev);	

	retval = gamepad_init_input(dev);
	if (retval)
	{
		goto error_deinit_output;
	}

	/* let the user know what node this device is now attached to */
	dev_info(&interface->dev, "Gamepad device now attached to USBgp-%d",
		 interface->minor);
	return 0;

error_deinit_output:
	gamepad_deinit_output(dev);
error:
	/* this frees allocated memory */
	kref_put(&dev->kref, gamepad_delete);

	return retval;
}

/*
	The disconnect function is called when the driver should no
	longer control the device for some reason and can do clean-up.
*/
static void gamepad_disconnect(struct usb_interface *interface)
{
	printk(KERN_ALERT "gamepad_disconnect\n");
	struct usb_gamepad *dev = usb_get_intfdata(interface);

	gamepad_deinit_input(dev);

	if (!usb_wait_anchor_empty_timeout(&dev->int_out_anchor, 5000))
	{
		dev_warn(&dev->interface->dev, "timed out waiting for output URB to complete, killing\n");
		usb_kill_anchored_urbs(&dev->int_out_anchor);
	}

	gamepad_deinit_output(dev);

	usb_free_urb(dev->int_in_urb);
	usb_free_coherent(dev->udev, GAMEPAD_PKT_LEN, dev->idata, dev->idata_dma);

	kfree(dev);
	usb_set_intfdata(interface, NULL);
}

static struct usb_driver gamepad_driver = {
	.name =		"maxdriver",
	.probe =	gamepad_probe,
	.disconnect =	gamepad_disconnect,
	.id_table =	gamepad_table,
	.supports_autosuspend = 1,
};

module_usb_driver(gamepad_driver);

MODULE_LICENSE("GPL");
