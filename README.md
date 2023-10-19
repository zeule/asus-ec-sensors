# asus-ec-sensors

## Linux HWMON sensors driver for ASUS motherboards to read sensor data from the embedded controller

Many ASUS motherboards do not publish all the available sensors via the Super I/O chip but the missing ones are
available through the embedded controller (EC) registers.

The EC registers do not provide critical values for the sensors and as such they are not published to the HWMON.

This driver is available in the mainline kernel since version 5.18, and code in this repository is kept in sync with
the hwmon-next branch.

## Supported motherboards

 * PRIME X470-PRO
 * PRIME X570-PRO
 * Pro WS X570-ACE
 * ProArt X570-CREATOR WIFI
 * ProArt B550-CREATOR
 * ROG CROSSHAIR VIII DARK HERO
 * ROG CROSSHAIR VIII HERO (WI-FI)
 * ROG CROSSHAIR VIII FORMULA
 * ROG CROSSHAIR VIII HERO
 * ROG CROSSHAIR VIII IMPACT
 * ROG CROSSHAIR X670E HERO
 * ROG MAXIMUS XI HERO
 * ROG MAXIMUS XI HERO (WI-FI)
 * ROG STRIX B550-E GAMING
 * ROG STRIX B550-I GAMING
 * ROG STRIX X570-E GAMING
 * ROG STRIX X570-E GAMING WIFI II
 * ROG STRIX X570-F GAMING
 * ROG STRIX X570-I GAMING
 * ROG STRIX X670E-E GAMING WIFI
 * ROG STRIX Z390-F GAMING
 * ROG STRIX Z690-A GAMING WIFI D4
 * ROG ZENITH II EXTREME
 * ROG ZENITH II EXTREME ALPHA

## Installation

[Gentoo ebuild](https://github.com/zeule/gentoo-zeule/tree/master/sys-power/asus-ec-sensors) and 
[AUR package](https://aur.archlinux.org/packages/asus-ec-sensors-dkms-git) are available. Alternatively,
you can clone the repository and then use standard `make` and `make modules_install` (as root) commands.
If you use DKMS, `make dkms` will build the module and add it to the DKMS tree for future updates.

## Adding a new motherboard

You can use other monitoring software to learn whether the motherboard provide sensor data via the EC. For example,
[HWiNFO64](https://www.hwinfo.com/) shows node named "ASUS EC" for these sensors.

[Libre Hardware Monitor](https://github.com/LibreHardwareMonitor/LibreHardwareMonitor) contains a very similar to this
driver
[implementation](https://github.com/LibreHardwareMonitor/LibreHardwareMonitor/blob/master/LibreHardwareMonitorLib/Hardware/Motherboard/Lpc/EC/EmbeddedController.cs)
for EC sensors, you can look there or even test and implement support for your board with Libre Hardware Monitor first.

First, you need identification data for your board, which are vendor and board names. You can get them from DMI tables
using either the `dmidecode` utility or run `grep -e ''  -n /sys/class/dmi/id/board_{name,vendor}`. If the vendor name
equals (case sensitively) to "ASUSTeK COMPUTER INC." you simply add a new entry to the `dmi_table` array (please keep it
sorted alphabetically by board name) using the `DMI_EXACT_MATCH_ASUS_BOARD_NAME` macro. If the vendor name is
different, please create a similar macro that accepts also the vendor name as a parameter.

The second step is to find out which sensors are supported and create a `ec_board_info` structure with sensor
definitions for the board. You can get a hint from HWINFO if it supports your board, from the monitoring section of the
UEFI user interface, or from information for similar boards. Please note differences in sensor addresses for various
board families.

The last step is to find out how to secure access to the EC from race condition, because the firmware does access the
same data. If you are lucky, the name of a ACPI mutex, which is used for synchronising access to the EC, can be found
out in the decompiled code of the ACPI firmware.

First, you need to obtain ACPI DSDT source for your board. You can get it from `/sys/firmware/acpi/tables/DSDT` 
in the binary form and decompile using the iasl tool from the acpica package:
```shell
$ sudo cat /sys/firmware/acpi/tables/DSDT > dsdt.dat
$ iasl dsdt.dat
```
This will produce decompiled file `dsdt.dsl` which you need to read and gather some information from it. Now you are on
your own. Look for the definition of the EC device (PNP0C09) and for ACPI methods which access its address space. For
example, in the AMD 500 series, there is a function named `BREC` (probably "Block Read Embedded Controller"). Its
declaration starts with `Method (BREC, 1`. If the function contains meaningful code, read through it a bit. Not far from
the beginning it should contain a command to lock a mutex. The command in the AML (the language of the ACPI code) is
named `Acquire` and the instructions should look like `If ((Acquire (ASMX, 0xFFFF) == Zero))`. You need the name of the
mutex, here it is "ASMX". Now find its declaration (`Mutex (ASMX, 0x00)`). It will be declared inside a scope, with the
scope declaration looking as `Device (<Device name>)`, find the closest one to the mutex declaration towards the top of
the file. Now compose the full name of the mutex, which starts from the '\' and then consists of device name(s) and the
mutex name. In the example above the name is `\AMW0.ASMX`. If you can't find mutex name, as the last resort you can use
the global ACPI lock.

Now you can write down a definition for `ec_board_info` structure for your board, hook its address into the board
identification array `dmi_table`, compile, and try to load the module (you might need to run `make install` in order to
make the build system to sign the module, if your kernel rejects unsigned ones). Then the
`sensors` command should show an entry named "asusec-..." with the sensor readings (`sensors 'asusec-*'` will show only
that entry). Please note the blank value for temperature sensors: 216.
