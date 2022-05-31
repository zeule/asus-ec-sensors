# asus-ec-sensors

## Linux HWMON sensors driver for ASUS motherboards to read sensor data from the embedded controller

Many ASUS motherboards do not publish all the available sensors via the Super I/O chip but the 
missing ones are available through the embedded controller (EC) registers.

This driver (unlike [the wmi-based one](https://github.com/zeule/asus-wmi-ec-sensors)) reads sensor values 
directly from the EC using the ec kernel module. However, to ensure no race between this driver and the firmware,
it locks the same mutex used by the WMI functions when accessing EC. This mutex has the same name (`\AMW0.ASMX`)
for all ASUS boards author seen DSDT code from (a bit more then 10 models).

The EC registers do not provide critical values for the sensors and as such they are not published to 
the HWMON.

## Supported motherboards

 * PRIME X470-PRO
 * PRIME X570-PRO
 * Pro WS X570-ACE
 * ProArt X570-CREATOR WIFI
 * ROG CROSSHAIR VIII DARK HERO
 * ROG CROSSHAIR VIII HERO (WI-FI)
 * ROG CROSSHAIR VIII FORMULA
 * ROG CROSSHAIR VIII HERO
 * ROG CROSSHAIR VIII IMPACT
 * ROG STRIX B550-E GAMING
 * ROG STRIX B550-I GAMING
 * ROG STRIX X570-E GAMING
 * ROG STRIX X570-E GAMING WIFI II
 * ROG STRIX X570-F GAMING
 * ROG STRIX X570-I GAMING
  * ROG STRIX Z690-A GAMING WIFI D4

## Installation

[Gentoo ebuild](https://github.com/zeule/gentoo-zeule/tree/master/sys-power/asus-ec-sensors) and 
[AUR package](https://aur.archlinux.org/packages/asus-ec-sensors-dkms-git) are available. Alternatively,
you can clone the repository and then use standard `make` and `make modules_install` (as root) commands.
If you use DKMS, `make dkms` will build the module and add it to the DKMS tree for future updates.

## Adding a new motherboard

First, you need to obtain ACPI DSDT source for your board. You can get it from `/sys/firmware/acpi/tables/DSDT` 
in the binary form and decompile using the iasl tool from the acpica package:
```shell
$ sudo cat /sys/firmware/acpi/tables/DSDT > dsdt.dat
$ iasl dsdt.dat
```
This will produce decompiled file `dsdt.dsl` which you need to read and gather some information from it.
Find a function named `BREC`. Its declaration starts with `Method (BREC, 1`. This is the WMI function to read 
EC registers ("Block Read EC" or something). If the function is a dummy one, like this:
```aml
Method (BREC, 1, Serialized)
{
       Return (Ones)
}
```
your motherboard probably does not publish any sensors in the EC. If the function contains meaningful code, read through
it a bit. Not far from the beginning it should contain a command to lock a mutex. The command in the AML (the language of
the ACPI code) is named `Acquire` and the instructions should look like `If ((Acquire (ASMX, 0xFFFF) == Zero))`. You need
the name of the mutex, here it is "ASMX". Now find its declaration (`Mutex (ASMX, 0x00)`). It will be declared inside 
a scope, with the scope declaration looking as `Device (<Device name>)`, find the closest one to the mutex declaration
towards the top of the file. Now compose the full name of the mutex, which starts from the '\' and then consists of
device name(s) and the mutex name. In the example above the name is `\AMW0.ASMX`. If you get another name, write it down 
as you will need it later.


Now find out which EC registers to read (although they are pretty typical). If you have 
no idea is there any, you can try the [hwinfo](https://www.hwinfo.com/) software under Windows that will
show the EC node and known sensors for it. When you have the data (or decided to probe the default set),
the following changes to the source code need to be made:

1. Add your board vendor and name to the `asus_ec_dmi_table` array and a new enum value to the `board` enum.
You can find the board vendor and the board name in `/sys/class/dmi/id/board_vendor` and
`/sys/class/dmi/id/board_name` or using `dmidecode`.

2. Add new entry to the `known_boards` array where list sensors for the board and the path for the ACPI mutex
which is used by the firmware to lock access to the EC. You can find this path in the `BREC` function, look for
`Acquire()` call near the top of the function.

Example: `If ((Acquire (ASMX, 0xFFFF) == Zero))`. The mutex name is `ASMX`, and since the `BREC` method is inside
the `AMW0` object, the mutex path is `\AMW0.ASMX`.

3. If you discover new sensors, modify the `known_ec_sensor` enum and add it to the `known_ec_sensors` array.
For each sensor you need to provide its size in bytes (for example, RPM counters span two single-byte registers),
its bank index and register index within the bank. If the sensor spans two or more registers, provide the 
first one (the smaller number).

Compile and it should work.
