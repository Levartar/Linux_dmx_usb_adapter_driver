# DMX USB Driver

The DMX USB driver is a Linux 6.X kernel driver for the Lixada USB dongle

## This Driver does not work currently
Another option is using LibUSB and directly transfering data to the adapter. This does not need a driver and works perfectly fine
```
int ret = libusb_control_transfer(handle,
                                      LIBUSB_ENDPOINT_OUT | LIBUSB_REQUEST_TYPE_VENDOR | LIBUSB_RECIPIENT_DEVICE,
                                      2,               // Command: Set Channel Range
                                      8,               // wValue: Number of channels to set (8 channels)
                                      0,               // wIndex: Starting at channel 1 (index 0)
                                      data,            // Data buffer containing the values
                                      sizeof(data),    // Length of the data buffer (8 bytes)
                                      5000);           // Timeout in milliseconds
```


## Building the driver

Before the driver can be build, the kernel sourcecode needs to be 
installed. To build the driver just call make, this should build a
`dmx_usb.ko` (the kernel module) and a `dmx_usb_test` (a small test
program).

## Building on a RPi4 running Raspbian

First update your RPi with the following commands;

```
apt-get update -y
apt-get upgrade -y
```

After that you might want to reboot to make sure you are running the
maybe updated kernel.

Than get the kernel source code;
!!KERNEL Source needs to be changed to https://github.com/RPi-Distro/rpi-source?tab=readme-ov-file#examples-on-how-to-build-various-modules !!

```
# Get rpi-source
sudo wget https://raw.githubusercontent.com/notro/rpi-source/master/rpi-source -O /usr/bin/rpi-source

# Make it executable
sudo chmod +x /usr/bin/rpi-source

# Tell the update mechanism that this is the latest version of the script
/usr/bin/rpi-source -q --tag-update

# Get the kernel files thingies.
rpi-source
```

Now you should be ready to build the DMX USB module.



