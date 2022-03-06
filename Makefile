_do_kbuild =
ifneq ($(KERNELRELEASE),)
	_do_kbuild = true
endif
ifneq ($(DKMS_BUILD),)
	_do_kbuild = undefined
endif

ifdef ($(_do_kbuild))
include Kbuild
else
KVER ?= $(shell uname -r)
KDIR ?= /lib/modules/$(KVER)
KBDIR = $(KDIR)/build

MAKEFLAGS += --no-print-directory
MOD_SUBDIR = extra/drivers/hwmon

ifneq (,$(wildcard .git/*))
	PACKAGE_VERSION := $(shell git describe --long --always)
else
	ifneq ("", "$(wildcard VERSION)")
		PACKAGE_VERSION := $(shell cat VERSION)
	else
		PACKAGE_VERSION := unknown
	endif
endif

ASUS_EC_SENSORS_CFLAGS=""

KVER_NUM = $(word 1, $(subst -, ,$(KVER)))

HAVE_MILLI := $(shell /bin/echo -e "5.15\n$(KVER_NUM)"|\
sort -Ct. -k1,1n -k2,2n && echo YES)

ifneq ("YES", "$(HAVE_MILLI)")
	ASUS_EC_SENSORS_CFLAGS +=-DMILLI=1000UL
endif

.PHONY: all install modules modules_install clean dkms dkms_clean dkms_configure
all: modules
modules modules_install clean:
	@$(MAKE) EXTRA_CFLAGS="$(ASUS_EC_SENSORS_CFLAGS)" \
		INSTALL_MOD_DIR=$(MOD_SUBDIR) -C $(KBDIR) M=$(CURDIR) $@

# DKMS
DRIVER_NAME = asus-ec-sensors

dkms_configure:
	@sed -e 's/@PACKAGE_VERSION@/$(PACKAGE_VERSION)/' \
	     -e 's/@PACKAGE_NAME@/$(DRIVER_NAME)/' \
	     -e 's/@BUILT_MODULE_NAME@/$(DRIVER_NAME)/' \
	     -e 's:@DEST_MODULE_LOCATION@:/$(MOD_SUBDIR):' dkms.conf.am >dkms.conf
	@echo "$(PACKAGE_VERSION)" >VERSION
dkms: dkms_configure
	@dkms add $(CURDIR)
	@dkms build -m $(DRIVER_NAME) -v $(PACKAGE_VERSION)
	@dkms install --force -m $(DRIVER_NAME) -v $(PACKAGE_VERSION)
	@modprobe $(DRIVER_NAME)

dkms_clean:
	-@rmmod $(DRIVER_NAME);
	-@dkms remove -m $(DRIVER_NAME) -v $(PACKAGE_VERSION)
	-@rm dkms.conf VERSION
endif
