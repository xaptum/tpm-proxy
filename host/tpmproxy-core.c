// SPDX-License-Identifier: GPL-2.0
/*
 *  Copyright (c) 2018 Xaptum, Inc.
 *
 * USB TPM Proxy Driver - 2.2
 *
 */

#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/kref.h>
#include <linux/uaccess.h>
#include <linux/usb.h>
#include <linux/mutex.h>

#include "tpmproxy-backports.h"

/* Define these values to match your devices */
#define USB_TPMP_VENDOR_ID      0x2FE0
#define USB_TPMP_PRODUCT_ID     0x7B01

/* table of devices that work with this driver */
static const struct usb_device_id tpmp_table[] = {
	{ USB_DEVICE(USB_TPMP_VENDOR_ID, USB_TPMP_PRODUCT_ID) },
	{ }					/* Terminating entry */
};
MODULE_DEVICE_TABLE(usb, tpmp_table);

/* Get a minor range for your devices from the usb maintainer */
#define USB_TPMP_MINOR_BASE	 192 + 2

/* arbitrarily chosen */

#define TPM_BUFSIZE 4096
#define TPMP_USB_TIMEOUT_MS	1000 // msecs

/* Structure to hold all of our device specific stuff */
struct usb_tpmp {
	struct usb_device	*udev;			/* the usb device for this device */
	struct usb_interface 	*interface;		/* the interface for this device */
	__u8			bulk_in_endpointAddr;	/* the address of the bulk in endpoint */
	__u8			bulk_out_endpointAddr;	/* the address of the bulk out endpoint */
	struct kref		kref;			/* Reference counter */
	struct mutex		usb_mutex;		/* synchronize I/O with disconnect */
	struct mutex		buffer_mutex;		/* mutex for buffer access */
	u8 			*data_buffer;		/* Buffer to hold the outgoing and incoming memory in user space */
	atomic_t 		data_pending;		/* Counter to the number of bytes waiting to be read into userspace */
	unsigned long 		is_open;		/* Flag for preventing double file access */ 
};
#define to_tpmp_dev(d) container_of(d, struct usb_tpmp, kref)

static struct usb_driver tpmp_driver;

static void tpmp_delete(struct kref *kref)
{
	struct usb_tpmp *dev = to_tpmp_dev(kref);

	usb_put_dev(dev->udev);
	kfree(dev->data_buffer);
	kfree(dev);
}

static int tpmp_open(struct inode *inode, struct file *file)
{
	struct usb_tpmp *dev;
	struct usb_interface *interface;
	int subminor;
	int retval = 0;

	subminor = iminor(inode);

	interface = usb_find_interface(&tpmp_driver, subminor);
	if (!interface) {
		pr_err("%s - error, can't find device for minor %d\n",
			__func__, subminor);
		retval = -ENODEV;
		goto exit;
	}

	dev = usb_get_intfdata(interface);
	if (!dev) {
		retval = -ENODEV;
		goto exit;
	}

	/* Don't allow multiple opens */
	if(test_and_set_bit(0,&dev->is_open))
		return -EBUSY;

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

static int tpmp_release(struct inode *inode, struct file *file)
{
	struct usb_tpmp *dev;

	dev = file->private_data;
	if (dev == NULL)
		return -ENODEV;

	/* allow the device to be autosuspended */
	mutex_lock(&dev->usb_mutex);
	if (dev->interface)
		usb_autopm_put_interface(dev->interface);
	mutex_unlock(&dev->usb_mutex);

	/* decrement the count on our device */
	kref_put(&dev->kref, tpmp_delete);

	/* Clear the flag preventing opens */
	clear_bit(0, &dev->is_open);

	return 0;
}

/**
 * tpmp_read() - Copy the last uncopied response from USB (if any) to userspace.
 * @file: File pointer
 * @buffer: Userspace buffer to copy to
 * @count: Not used
 * @ppos: Not used
 *
 * Reads the tpmp response from the last usb communication into userspace.
 * Will read up to 4096 bytes and return, reads will never be larger and
 * won't need a second call to complete a partial read.
 *
 * Return:
		Number of bytes copied if >=0
		-EFAULT on memory copy error
 */
static ssize_t tpmp_read(struct file *file, char __user *buffer, size_t count,
			 loff_t *ppos)
{
	struct usb_tpmp *dev;
	ssize_t bytes_copied;
	ssize_t bytes_to_copy;

	dev = file->private_data;
	bytes_copied = 0;

	/* Lock the buffer memory mutex */
	mutex_lock(&dev->buffer_mutex);
	bytes_to_copy = atomic_read(&dev->data_pending);

	/* If we have anything to copy */
	if (bytes_to_copy != 0) {
		/* Copy data into userspace */
		if (copy_to_user(buffer,
				 dev->data_buffer,
				 bytes_to_copy)) {
			bytes_copied = -EFAULT;
		}
		else {
			bytes_copied = bytes_to_copy;
		}

		/* Clear data pending flag */
		atomic_set(&dev->data_pending, 0);
	} 

	mutex_unlock(&dev->buffer_mutex);
	return bytes_copied;
}



/**
 * tpmp_write() - Write data to USB and copy the response into kernel space memory
 * @file: File pointer
 * @buffer: Userspace buffer to send to USB
 * @count: Number of byes to send
 * @ppos: Not used
 *
 * Writes the given buffer to USB then reads the response and stores it. The response 
 * can be read via tpmp_read. Will not overwrite data from a previous call. 
 *
 * Return: 
 	Number of bytes copied if >=0
	-E2BIG on count exceeding max write size
	-EBUSY on buffer already being occupied
	-EFAULT on memory copy error
 */
static ssize_t tpmp_write(struct file *file, const char __user *user_buffer,
			  size_t count, loff_t *ppos)
{
	struct usb_tpmp *dev;
	int retval = 0;
	unsigned int pipe;
	int actual_len_recvd;
	int actual_len_sent;

	/* Initialize local variables */
	dev = file->private_data;
	actual_len_recvd=0;
	actual_len_sent=0;


	/* Verify the requested data isnt too large */
	if (count > TPM_BUFSIZE) {
		actual_len_sent = -E2BIG;
		goto err;
	}

	/* Lock the buffer memory mutex */
	mutex_lock(&dev->buffer_mutex);

	/* Make sure we aren't overriding anything */
	if (atomic_read(&dev->data_pending) != 0) {
		actual_len_sent = -EBUSY;
		goto err_unlock_buffer;
	}

	/* Copy message to kernel space */
	if (copy_from_user(dev->data_buffer, user_buffer, count)) {
		actual_len_sent = -EFAULT;
		goto err_unlock_buffer;
	}

	/* Write to the USB device */
	mutex_lock(&dev->usb_mutex);

	//Make sure the USB device is still open
	if(!dev->interface) {
		actual_len_sent = -ENODEV;
		goto err_unlock_usb;
	}

	pipe = usb_sndbulkpipe(dev->udev, dev->bulk_out_endpointAddr);
	retval = usb_bulk_msg(dev->udev, pipe, dev->data_buffer, count, &actual_len_sent,
		TPMP_USB_TIMEOUT_MS);

	if(retval) {
		actual_len_sent=retval;
		goto err_unlock_usb;
	}

	/* Read from the device into our buffer */
	pipe = usb_rcvbulkpipe(dev->udev, dev->bulk_in_endpointAddr);
	retval = usb_bulk_msg(dev->udev, pipe, dev->data_buffer, TPM_BUFSIZE, &actual_len_recvd,
		TPMP_USB_TIMEOUT_MS);

	if(retval) {
		actual_len_sent=retval;
		goto err_unlock_usb;
	}

	/* Record the number of bytes recieved */
	atomic_set(&dev->data_pending, actual_len_recvd);

	err_unlock_usb:
	mutex_unlock(&dev->usb_mutex);

	err_unlock_buffer:
	mutex_unlock(&dev->buffer_mutex);

	err:
	return actual_len_sent;
}

static const struct file_operations tpmp_fops = {
	.owner =	THIS_MODULE,
	.read =	tpmp_read,
	.write =	tpmp_write,
	.open =	tpmp_open,
	.release =	tpmp_release,
	.llseek =	no_llseek,
};

/*
 * usb class driver info in order to get a minor number from the usb core,
 * and to have the device registered with the driver core
 */
static struct usb_class_driver tpmp_class = {
	.name       = "tpmp%d",
	.fops       = &tpmp_fops,
	.minor_base = USB_TPMP_MINOR_BASE,
};

static int tpmp_probe(struct usb_interface *interface,
		      const struct usb_device_id *id)
{
	struct usb_tpmp *dev;
	struct usb_endpoint_descriptor *bulk_in, *bulk_out;
	int retval;

	/* allocate memory for our device state and initialize it */
	dev = kzalloc(sizeof(*dev), GFP_KERNEL);
	if (!dev)
		return -ENOMEM;

	kref_init(&dev->kref);
	mutex_init(&dev->usb_mutex);
	mutex_init(&dev->buffer_mutex);

	dev->udev = usb_get_dev(interface_to_usbdev(interface));
	dev->interface = interface;
	dev->is_open=0;

	/* set up the endpoint information */
	/* use only the first bulk-in and bulk-out endpoints */
	retval = usb_find_common_endpoints(interface->cur_altsetting,
			&bulk_in, &bulk_out, NULL, NULL);
	if (retval) {
		dev_err(&interface->dev,
			"Could not find both bulk-in and bulk-out endpoints\n");
		goto error;
	}

	dev->bulk_in_endpointAddr = bulk_in->bEndpointAddress;
	dev->data_buffer = kmalloc(TPM_BUFSIZE,GFP_KERNEL);
	if (!dev->data_buffer) {
		retval = -ENOMEM;
		goto error;
	}

	dev->bulk_out_endpointAddr = bulk_out->bEndpointAddress;

	/* save our data pointer in this interface device */
	usb_set_intfdata(interface, dev);

	/* we can register the device now, as it is ready */
	retval = usb_register_dev(interface, &tpmp_class);
	if (retval) {
		/* something prevented us from registering this driver */
		dev_err(&interface->dev,
			"Not able to get a minor for this device.\n");
		usb_set_intfdata(interface, NULL);
		goto error;
	}

	/* let the user know what node this device is now attached to */
	dev_info(&interface->dev,
		 "USB TPM proxy device now attached to tpmp%d",
		 interface->minor);
	return 0;

error:
	/* this frees allocated memory */
	kref_put(&dev->kref, tpmp_delete);

	return retval;
}

static void tpmp_disconnect(struct usb_interface *interface)
{
	struct usb_tpmp *dev;
	int minor = interface->minor;

	dev = usb_get_intfdata(interface);
	usb_set_intfdata(interface, NULL);

	/* give back our minor */
	usb_deregister_dev(interface, &tpmp_class);

	/* prevent more I/O from starting */
	mutex_lock(&dev->usb_mutex);
	dev->interface = NULL;
	mutex_unlock(&dev->usb_mutex);

	/* decrement our usage count */
	kref_put(&dev->kref, tpmp_delete);

	dev_info(&interface->dev, "USB TPM proxy #%d now disconnected", minor);
}

static int tpmp_suspend(struct usb_interface *intf, pm_message_t message)
{
	struct usb_tpmp *dev = usb_get_intfdata(intf);

	if (!dev)
		return 0;
	return 0;
}

static int tpmp_resume(struct usb_interface *intf)
{
	return 0;
}

static int tpmp_pre_reset(struct usb_interface *intf)
{
	struct usb_tpmp *dev = usb_get_intfdata(intf);

	mutex_lock(&dev->usb_mutex);

	return 0;
}

static int tpmp_post_reset(struct usb_interface *intf)
{
	struct usb_tpmp *dev = usb_get_intfdata(intf);

	/* we are sure no URBs are active - no locking needed */
	mutex_unlock(&dev->usb_mutex);

	return 0;
}

static struct usb_driver tpmp_driver = {
	.name        = "tpmproxy",
	.probe       =  tpmp_probe,
	.disconnect  =  tpmp_disconnect,
	.suspend     =  tpmp_suspend,
	.resume      =  tpmp_resume,
	.pre_reset   =  tpmp_pre_reset,
	.post_reset  =  tpmp_post_reset,
	.id_table    =  tpmp_table,
	.supports_autosuspend = 1,
};

module_usb_driver(tpmp_driver);

MODULE_LICENSE("GPL v2");
