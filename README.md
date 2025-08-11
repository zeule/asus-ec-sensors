# asus-ec-sensors

## Linux HWMON sensors driver for ASUS motherboards to read sensor data from the embedded controller

Many ASUS motherboards do not publish all the available sensors via the Super I/O chip but the missing ones are
available through the embedded controller (EC) registers.

The EC registers do not provide critical values for the sensors and as such they are not published to the HWMON.

This driver is available in the mainline kernel since version 5.18, and code in this repository is kept in sync with
the hwmon-next branch.

## Supported motherboards

 * MAXIMUS VI HERO
 * PRIME X470-PRO
 * PRIME X570-PRO
 * PRIME X670E-PRO WIFI
 * Pro WS X570-ACE
 * ProArt X570-CREATOR WIFI
 * ProArt X670E-CREATOR WIFI
 * ProArt X870E-CREATOR WIFI
 * ProArt B550-CREATOR
 * ROG CROSSHAIR VIII DARK HERO
 * ROG CROSSHAIR VIII HERO (WI-FI)
 * ROG CROSSHAIR VIII FORMULA
 * ROG CROSSHAIR VIII HERO
 * ROG CROSSHAIR VIII IMPACT
 * ROG CROSSHAIR X670E HERO
 * ROG CROSSHAIR X670E GENE
 * ROG MAXIMUS XI HERO
 * ROG MAXIMUS XI HERO (WI-FI)
 * ROG MAXIMUS Z690 FORMULA
 * ROG STRIX B550-E GAMING
 * ROG STRIX B550-I GAMING
 * ROG STRIX B650E-I GAMING WIFI
 * ROG STRIX B850-I GAMING WIFI
 * ROG STRIX X570-E GAMING
 * ROG STRIX X570-E GAMING WIFI II
 * ROG STRIX X570-F GAMING
 * ROG STRIX X570-I GAMING
 * ROG STRIX X670E-I GAMING WIFI
 * ROG STRIX X870E-E GAMING WIFI
 * ROG STRIX Z390-F GAMING
 * ROG STRIX Z490-F GAMING
 * ROG STRIX Z690-A GAMING WIFI D4
 * ROG STRIX Z790-E GAMING WIFI II
 * ROG STRIX Z790-I GAMING WIFI
 * ROG ZENITH II EXTREME
 * ROG ZENITH II EXTREME ALPHA
 * TUF GAMING X670E PLUS

## Installation

[Gentoo ebuild](https://github.com/zeule/gentoo-zeule/tree/master/sys-power/asus-ec-sensors) and 
[AUR package](https://aur.archlinux.org/packages/asus-ec-sensors-dkms-git) are available. Alternatively,
you can clone the repository and then use standard `make` and `make modules_install` (as root) commands.
If you use DKMS, `make dkms` will build the module and add it to the DKMS tree for future updates.

## Adding a new motherboard

Please see [here](CONTRIBUTING.md).
