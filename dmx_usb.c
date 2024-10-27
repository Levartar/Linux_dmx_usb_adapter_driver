#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/usb.h>
#include <linux/slab.h>
#include <linux/fs.h>      // for character device
#include <linux/uaccess.h> // for copy_to_user and copy_from_user
#include <linux/miscdevice.h>

#define USBDEV_SHARED_VENDOR    0x16C0  /* Vendor ID for uDMX */
#define USBDEV_SHARED_PRODUCT   0x05DC  /* Product ID for uDMX */
#define DMX_CHANNELS            512     /* Total DMX channels */

struct dmx_usb_device {
    struct usb_device *udev; /* save off the usb device pointer */
	struct usb_interface *interface; /* the interface for this device */
    unsigned char dmx_data[DMX_CHANNELS];
    unsigned char minor; /* the starting minor number for this device */

    unsigned char *		bulk_in_buffer;		/* the buffer to receive data */
	size_t			bulk_in_size;		/* the size of the receive buffer */
	__u8			bulk_in_endpointAddr;	/* the address of the bulk in endpoint */

	unsigned char *		bulk_out_buffer;	/* the buffer to send data */
	size_t			bulk_out_size;		/* the size of the send buffer */
	size_t			bulk_out_alloc_size; 
    struct urb *write_urb;/* the urb used to send data */
    __u8			bulk_out_endpointAddr;	/* the address of the bulk out endpoint */
    atomic_t		write_busy;		/* true iff write urb is busy */
	struct completion	write_finished;		/* wait for the write to finish */

    int	open; /* if the port is open or not */
    int	present; /* if the device is not disconnected */
    struct semaphore sem; /* locks this structure */
};

/* local function prototypes */
static ssize_t dmx_usb_write	(struct file *file, const char *buffer, size_t count, loff_t *ppos);
//static long dmx_usb_ioctl	(struct file *file, unsigned int cmd, unsigned long arg);
static int dmx_usb_open		(struct inode *inode, struct file *file);
static int dmx_usb_release	(struct inode *inode, struct file *file);

static int dmx_usb_probe	(struct usb_interface *interface, const struct usb_device_id *id);
static void dmx_usb_disconnect	(struct usb_interface *interface);

/* USB device table */
static struct usb_device_id dmx_usb_table[] = {
    { USB_DEVICE(USBDEV_SHARED_VENDOR, USBDEV_SHARED_PRODUCT) },
    { }
};
MODULE_DEVICE_TABLE(usb, dmx_usb_table);


/**
 */
//static long dmx_usb_ioctl (struct file *file, unsigned int cmd, unsigned long arg)
//{
//	struct dmx_usb_device *dev;
//
//	dev = (struct dmx_usb_device *)file->private_data;
//
//	/* lock this object */
//	down (&dev->sem);
//
//	/* verify that the device wasn't unplugged */
//	if (!dev->present) {
//		up (&dev->sem);
//		return -ENODEV;
//	}
//
//	dbg("%s - minor %d, cmd 0x%.4x, arg %lu", __FUNCTION__,
//	    dev->minor, cmd, arg);
//
//	/* fill in your device specific stuff here */
//
//	/* unlock the device */
//	up (&dev->sem);
//
//	/* return that we did not understand this ioctl call */
//	return -ENOTTY;
//}


/* File operations for the /dev/dmx_usb character device */
static const struct file_operations dmx_usb_fops = {
    	/*
	 * The owner field is part of the module-locking
	 * mechanism. The idea is that the kernel knows
	 * which module to increment the use-counter of
	 * BEFORE it calls the device's open() function.
	 * This also means that the kernel can decrement
	 * the use-counter again before calling release()
	 * or should the open() function fail.
	 */
    .owner = THIS_MODULE,

    .open = dmx_usb_open,
    .write = dmx_usb_write,
    //.unlocked_ioctl =	dmx_usb_ioctl,
    .release = dmx_usb_release,
};

/*
 * usb class driver info in order to get a minor number from the usb core,
 * and to have the device registered with devfs and the driver core
 */
static struct usb_class_driver dmx_usb_class = {
	.name =		"usb/dmx%d",
	.fops =		&dmx_usb_fops,
	//.minor_base =	DMX_USB_MINOR_BASE,
};

/* usb specific object needed to register this driver with the usb subsystem */
static struct usb_driver dmx_usb_driver = {
	.name =		"dmx_usb",
	.probe =	dmx_usb_probe,
	.disconnect =	dmx_usb_disconnect,
	.id_table =	dmx_usb_table,
};


/* Create a misc device for /dev/dmx_usb */
static struct miscdevice dmx_usb_misc_device = {
    .minor = MISC_DYNAMIC_MINOR,
    .name = "dmx_usb",
    .fops = &dmx_usb_fops,
};

/* Write function to handle user-space writes to /dev/dmx_usb */
static ssize_t dmx_usb_write(struct file *file, const char __user *user_buf, size_t count, loff_t *ppos) {
    struct dmx_usb_device *dmxdev = file->private_data;
    unsigned char buffer[2];  // Buffer for sending data to USB
    int channel_number = 1;
    int intensity_value = 32;
    int cmd_SetSingleChannel = 2;
    int retval;

    /* Check for null pointers */
    if (!dmxdev || !dmxdev->udev) {
        pr_err("dmx_usb: Invalid device pointer in write operation\n");
        return -ENODEV;
    }

    /* Ensure count does not exceed buffer size */
    if (count > sizeof(buffer)) {
        pr_err("dmx_usb: Write count exceeds buffer limit\n");
        return -EINVAL;
    }

    /* Copy data from user space */
    memset(buffer, 0, sizeof(buffer));
    if (copy_from_user(buffer, user_buf, count)) {
        pr_err("dmx_usb: Failed to copy data from user buffer\n");
        return -EFAULT;
    }

    buffer[0] = 128;
    buffer[1] = 128;     // Set intensity in buffer, if needed

    /* Send DMX data to USB device */
    retval = usb_control_msg(dmxdev->udev,
                         usb_sndctrlpipe(dmxdev->udev, 0),
                         cmd_SetSingleChannel,         // Command for setting a single channel
                         USB_TYPE_VENDOR | USB_RECIP_DEVICE | USB_DIR_OUT,
                         intensity_value,              // Intensity or value to set
                         channel_number,               // The DMX channel to set
                         buffer,                       // Data buffer
                         sizeof(buffer),               // Size of data buffer
                         5000);                        // Timeout in ms
    
    /* Check if usb_control_msg completed successfully */
    if (retval < 0) {
        pr_err("dmx_usb: USB control message failed with error %d\n", retval);
        return retval;  // Return the USB error code if failure occurred
    }

    pr_info("dmx_usb: %zu bytes written to DMX device\n", count);
    return count;
}




static int dmx_usb_open(struct inode *inode, struct file *file) {
    struct dmx_usb_device *dev;
    struct usb_interface *interface;
    int subminor;

    subminor = iminor(inode);

	interface = usb_find_interface (&dmx_usb_driver, subminor);
	if (!interface) {
		dev_err(&interface->dev, "can't find device for minor");
		return -ENODEV;
	}

    /* Get the device from the inode */
    dev = container_of(file->private_data, struct dmx_usb_device, udev);
    if (!dev) {
        pr_err("dmx_usb: Null pointer in dmx_usb_open\n");
        return -ENODEV;
    }

    /* Save the dmx_usb_device pointer in file->private_data */
    file->private_data = dev;

    /* lock this device */
	down (&dev->sem);

	/* increment our usage count for the driver */
	++dev->open;

	/* save our object in the file's private structure */
	file->private_data = dev;

	/* unlock this device */
	up (&dev->sem);

    pr_info("DMX USB device opened\n");
    return 0;
}

static int dmx_usb_release(struct inode *inode, struct file *file) {
    struct usb_device *udev;
    struct dmx_usb_device *dev;

    dev = (struct dmx_usb_device *)file->private_data;
	if (dev == NULL) {
        dev_err(&udev->dev, "object is NULL");
		return -ENODEV;
	}

	if (dev->open <= 0) {
        dev_err(&udev->dev, "device not opened");
		return -ENODEV;
	}

    pr_info("DMX USB device closed\n");
    return 0;
}

/**
 *
 *	Called by the usb core when a new device is connected that it thinks
 *	this driver might be interested in.
 */
static int dmx_usb_probe(struct usb_interface *interface, const struct usb_device_id *id) {
    struct usb_device *udev = interface_to_usbdev(interface);
    struct dmx_usb_device *dev;
    int retval;

    dev_info(&udev->dev, "DMX USB adapter detected, initializing...\n");

    // Allocate memory for the device
    dev = kzalloc(sizeof(struct dmx_usb_device), GFP_KERNEL);
    if (!dev) {
        dev_err(&interface->dev, "Cannot allocate memory for DMX USB device\n");
        return -ENOMEM;
    }
    memset(dev, 0x00, sizeof(*dev));

    dev->udev = usb_get_dev(udev);
    dev->interface = interface;

    // Skip bulk endpoint setup and focus on control transfers
    dev_info(&udev->dev, "Using control transfers for DMX data.\n");

    // Set the interface (claim in kernel space)
    retval = usb_set_interface(udev, 0, 0); // interface 0, alternate setting 0
    if (retval) {
        dev_err(&udev->dev, "Cannot set interface, error: %d\n", retval);
        usb_put_dev(udev);
        kfree(dev);
        return retval;
    } else {
        dev_info(&udev->dev, "Interface set successfully.\n");
    }

    // Register the character device
    retval = misc_register(&dmx_usb_misc_device);
    if (retval) {
        dev_err(&udev->dev, "Failed to register character device\n");
        usb_put_dev(udev);
        kfree(dev);
        return retval;
    }

    usb_set_intfdata(interface, dev);
    dev_info(&udev->dev, "DMX USB adapter initialized successfully.\n");
    return 0;
}


/* Disconnect function (called when the device is unplugged) */
static void dmx_usb_disconnect(struct usb_interface *interface) {
    struct dmx_usb_device *dmxdev = usb_get_intfdata(interface);

    // Unregister the character device
    misc_deregister(&dmx_usb_misc_device);

    usb_set_intfdata(interface, NULL);
    usb_put_dev(dmxdev->udev);
    kfree(dmxdev);

    dev_info(&interface->dev, "DMX USB adapter disconnected.\n");
}

/* Initialize the driver (called when the module is loaded) */
static int __init dmx_usb_init(void) {
    int result;

    /* Register this driver with the USB subsystem */
    result = usb_register(&dmx_usb_driver);
    if (result) {
        pr_err("usb_register failed. Error number %d\n", result);
        return result;
    }

    pr_info("DMX USB Driver Initialized\n");
    return 0;
}

/* Exit the driver (called when the module is unloaded) */
static void __exit dmx_usb_exit(void) {
    /* Deregister this driver with the USB subsystem */
    usb_deregister(&dmx_usb_driver);
    pr_info("DMX USB Driver Unloaded\n");
}

module_init(dmx_usb_init);
module_exit(dmx_usb_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Jakob");
MODULE_DESCRIPTION("DMX USB Driver for Lixada USB-to-DMX512 Adapter");
