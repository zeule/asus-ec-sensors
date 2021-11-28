// SPDX-License-Identifier: GPL-2.0+
/*
 * HWMON driver for ASUS motherboards that publish some sensor values
 * via the embedded controller registers
 *
 * Copyright (C) 2021 Eugene Shalygin <eugene.shalygin@gmail.com>
 */

#include <asm/unaligned.h>
#include <linux/acpi.h>
#include <linux/dev_printk.h>
#include <linux/dmi.h>
#include <linux/hwmon.h>
#include <linux/init.h>
#include <linux/jiffies.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/sort.h>

#define ASUS_EC_BANK_REGISTER 0xff
#define MAX_SENSOR_LABEL_LENGTH 0x10
/*
 * Arbitrary set max allowed bank number. Required for sorting banks and
 * currently is overkill with just 2 banks used at max, but for the sake
 * of alignment let's set it to a higher value
 */
#define ASUS_EC_MAX_BANK 0x04

#define ACPI_DELAY_MSEC_LOCK	500	/* Wait 0.5 s max. to get the lock */
#define ASUS_HW_ACCESS_MUTEX_NAME	"\\AMW0.ASMX"

typedef union {
	u32 value;
	struct {
		u8 index;
		u8 bank;
		u8 size;
		u8 dummy;
	} components;
} sensor_address;

#define MAKE_SENSOR_ADDRESS(size, bank, index)                                 \
	{                                                                      \
		.value = (size << 16) + (bank << 8) + index                    \
	}


typedef u8 board_t;
enum board {
	BOARD_PW_X570_A, // Pro WS X570-ACE
	BOARD_R_C8H, // ROG Crosshair VIII Hero
	BOARD_R_C8DH, // ROG Crosshair VIII Dark Hero
	BOARD_R_C8F, // ROG Crosshair VIII Formula
	BOARD_R_C8I, // ROG CROSSHAIR VIII Impact
	BOARD_RS_B550_E_G, // ROG STRIX B550-E GAMING
	BOARD_RS_B550_I_G, // ROG STRIX B550-I GAMING
	BOARD_RS_X570_E_G, // ROG STRIX X570-E GAMING
	BOARD_MAX
};

#define DMI_EXACT_MATCH_ASUS_BOARD_NAME(name)                                  \
	{                                                                      \
		.matches = {                                                   \
			DMI_EXACT_MATCH(DMI_BOARD_VENDOR,                      \
					"ASUSTeK COMPUTER INC."),              \
			DMI_EXACT_MATCH(DMI_BOARD_NAME, name),                 \
		}                                                              \
	}

static const struct dmi_system_id asus_ec_dmi_table[BOARD_MAX + 1] __initconst = {
	[BOARD_PW_X570_A] = DMI_EXACT_MATCH_ASUS_BOARD_NAME("Pro WS X570-ACE"),
	[BOARD_R_C8H] = DMI_EXACT_MATCH_ASUS_BOARD_NAME("ROG CROSSHAIR VIII HERO"),
	[BOARD_R_C8DH] = DMI_EXACT_MATCH_ASUS_BOARD_NAME("ROG CROSSHAIR VIII DARK HERO"),
	[BOARD_R_C8F] = DMI_EXACT_MATCH_ASUS_BOARD_NAME("ROG CROSSHAIR VIII FORMULA"),
	[BOARD_R_C8I] = DMI_EXACT_MATCH_ASUS_BOARD_NAME("ROG CROSSHAIR VIII IMPACT"),
	[BOARD_RS_B550_E_G] = DMI_EXACT_MATCH_ASUS_BOARD_NAME("ROG STRIX B550-E GAMING"),
	[BOARD_RS_B550_I_G] = DMI_EXACT_MATCH_ASUS_BOARD_NAME("ROG STRIX B550-I GAMING"),
	[BOARD_RS_X570_E_G] = DMI_EXACT_MATCH_ASUS_BOARD_NAME("ROG STRIX X570-E GAMING"),
	[BOARD_MAX] = {}
};

static u32 hwmon_attributes[] = {
	[hwmon_chip] = HWMON_C_REGISTER_TZ,
	[hwmon_temp] = HWMON_T_INPUT | HWMON_T_LABEL,
	[hwmon_in] = HWMON_I_INPUT | HWMON_I_LABEL,
	[hwmon_curr] = HWMON_C_INPUT | HWMON_C_LABEL,
	[hwmon_fan] = HWMON_F_INPUT | HWMON_F_LABEL,
};

struct ec_sensor_info {
	char label[MAX_SENSOR_LABEL_LENGTH];
	enum hwmon_sensor_types type;
	sensor_address addr;
};

#define EC_SENSOR(sensor_label, sensor_type, size, bank, index)                \
	{                                                                      \
		.label = sensor_label, .type = sensor_type,                    \
		.addr = MAKE_SENSOR_ADDRESS(size, bank, index)                 \
	}

enum known_ec_sensor {
	SENSOR_TEMP_CHIPSET 	=   0x1, /* chipset temperature [℃] */
	SENSOR_TEMP_CPU		=   0x2, /* CPU temperature [℃] */
	SENSOR_TEMP_MB		=   0x4, /* motherboard temperature [℃] */
	SENSOR_TEMP_T_SENSOR	=   0x8, /* "T_Sensor" temperature sensor reading [℃] */
	SENSOR_TEMP_VRM		=  0x10, /* VRM temperature [℃] */
	SENSOR_FAN_CPU_OPT	=  0x20, /* CPU_Opt fan [RPM] */
	SENSOR_FAN_VRM_HS	=  0x40, /* VRM heat sink fan [RPM] */
	SENSOR_FAN_CHIPSET	=  0x80, /* chipset fan [RPM] */
	SENSOR_FAN_WATER_FLOW	= 0x100, /* water flow sensor reading [RPM] */
	SENSOR_CURR_CPU		= 0x200, /* CPU current [A] */
	SENSOR_TEMP_WATER_IN	= 0x400, /* "Water_In" temperature sensor reading [℃] */
	SENSOR_TEMP_WATER_OUT	= 0x800, /* "Water_Out" temperature sensor reading [℃] */
	SENSOR_MAX = SENSOR_TEMP_WATER_OUT
};

/*
 * All the known sensors for ASUS EC controllers
 */
static const struct ec_sensor_info known_ec_sensors[] = {
	EC_SENSOR("Chipset", hwmon_temp, 1, 0x00, 0x3a), /* SENSOR_TEMP_CHIPSET */
	EC_SENSOR("CPU", hwmon_temp, 1, 0x00, 0x3b), /* SENSOR_TEMP_CPU */
	EC_SENSOR("Motherboard", hwmon_temp, 1, 0x00, 0x3c), /* SENSOR_TEMP_MB */
	EC_SENSOR("T_Sensor", hwmon_temp, 1, 0x00, 0x3d), /* SENSOR_TEMP_T_SENSOR */
	EC_SENSOR("VRM", hwmon_temp, 1, 0x00, 0x3e), /* SENSOR_TEMP_VRM */
	EC_SENSOR("CPU_Opt", hwmon_fan, 2, 0x00, 0xb0), /* SENSOR_FAN_CPU_OPT */
	EC_SENSOR("VRM HS", hwmon_fan, 2, 0x00, 0xb2), /* SENSOR_FAN_VRM_HS */
	EC_SENSOR("Chipset", hwmon_fan, 2, 0x00, 0xb4), /* SENSOR_FAN_CHIPSET */
	EC_SENSOR("Water_Flow", hwmon_fan, 2, 0x00, 0xbc), /* SENSOR_FAN_WATER_FLOW */
	EC_SENSOR("CPU", hwmon_curr, 1, 0x00, 0xf4), /* SENSOR_CURR_CPU */
	EC_SENSOR("Water_In", hwmon_temp, 1, 0x01, 0x00), /* SENSOR_TEMP_WATER_IN */
	EC_SENSOR("Water_Out", hwmon_temp, 1, 0x01, 0x01), /* SENSOR_TEMP_WATER_OUT */
};

static const enum known_ec_sensor known_board_sensors[BOARD_MAX] __initconst = {
	[BOARD_PW_X570_A] =
		SENSOR_TEMP_CHIPSET | SENSOR_TEMP_CPU | SENSOR_TEMP_MB | SENSOR_TEMP_VRM |
		SENSOR_FAN_CHIPSET |
		SENSOR_CURR_CPU,
	[BOARD_R_C8H] =
		SENSOR_TEMP_CHIPSET | SENSOR_TEMP_CPU | SENSOR_TEMP_MB |
		SENSOR_TEMP_T_SENSOR | SENSOR_TEMP_VRM |
		SENSOR_TEMP_WATER_IN | SENSOR_TEMP_WATER_OUT |
		SENSOR_FAN_CPU_OPT | SENSOR_FAN_CHIPSET | SENSOR_FAN_WATER_FLOW |
		SENSOR_CURR_CPU,
	[BOARD_R_C8DH] = /* Same as Hero but without chipset fan */
		SENSOR_TEMP_CHIPSET | SENSOR_TEMP_CPU | SENSOR_TEMP_MB |
		SENSOR_TEMP_T_SENSOR | SENSOR_TEMP_VRM |
		SENSOR_TEMP_WATER_IN | SENSOR_TEMP_WATER_OUT |
		SENSOR_FAN_CPU_OPT | SENSOR_FAN_WATER_FLOW |
		SENSOR_CURR_CPU,
	[BOARD_R_C8F] = /* Same as Hero but without water */
		SENSOR_TEMP_CHIPSET | SENSOR_TEMP_CPU | SENSOR_TEMP_MB |
		SENSOR_TEMP_T_SENSOR | SENSOR_TEMP_VRM |
		SENSOR_FAN_CPU_OPT | SENSOR_FAN_CHIPSET |
		SENSOR_CURR_CPU,
	[BOARD_R_C8I] =
		SENSOR_TEMP_CHIPSET | SENSOR_TEMP_CPU | SENSOR_TEMP_MB |
		SENSOR_TEMP_T_SENSOR | SENSOR_TEMP_VRM |
		SENSOR_FAN_CHIPSET |
		SENSOR_CURR_CPU,
	[BOARD_RS_B550_E_G] =
		SENSOR_TEMP_CHIPSET | SENSOR_TEMP_CPU | SENSOR_TEMP_MB |
		SENSOR_TEMP_T_SENSOR | SENSOR_TEMP_VRM |
		SENSOR_FAN_CPU_OPT |
		SENSOR_CURR_CPU,
	[BOARD_RS_B550_I_G] =
		SENSOR_TEMP_CHIPSET | SENSOR_TEMP_CPU | SENSOR_TEMP_MB |
		SENSOR_TEMP_T_SENSOR | SENSOR_TEMP_VRM |
		SENSOR_FAN_VRM_HS |
		SENSOR_CURR_CPU,
	[BOARD_RS_X570_E_G] =
		SENSOR_TEMP_CHIPSET | SENSOR_TEMP_CPU | SENSOR_TEMP_MB |
		SENSOR_TEMP_T_SENSOR | SENSOR_TEMP_VRM |
		SENSOR_FAN_CHIPSET |
		SENSOR_CURR_CPU
};

struct ec_sensor {
	unsigned info_index;
	u32 cached_value;
};

struct ec_sensors_data {
	struct ec_sensor* sensors;
	/* EC registers to read from */
	u16* registers;
	/* sorted list of unique register banks */
	u8 banks[ASUS_EC_MAX_BANK];
	u8* read_buffer;
	unsigned long last_updated; /* in jiffies */
	acpi_handle aml_mutex;
	u8 nr_sensors; /* number of board EC sensors */
	/* number of EC registers to read (sensor might span more than 1 register) */
	u8 nr_registers;
	u8 nr_banks; /* number of unique register banks */
};

struct asus_ec_sensors {
	struct ec_sensors_data sensors_data;
	board_t board;
};

static inline u8 register_bank(u16 reg)
{
	return (reg & 0xff00) >> 8;
}

static inline struct ec_sensors_data *get_sensor_data(struct device *pdev)
{
	return &((struct asus_ec_sensors *)dev_get_drvdata(pdev))->sensors_data;
}

static inline const struct ec_sensor_info *
get_sensor_info(const struct ec_sensors_data *state, int index)
{
	return &known_ec_sensors[state->sensors[index].info_index];
}

static int find_ec_sensor_index(const struct ec_sensors_data *ec,
				enum hwmon_sensor_types type, int channel)
{
	unsigned i;

	for (i = 0; i < ec->nr_sensors; ++i) {
		if (get_sensor_info(ec, i)->type == type) {
			if (channel == 0) {
				return i;
			}
			--channel;
		}
	}
	return -ENOENT;
}

static int bank_compare(const void* a, const void* b)
{
	return *((const s8*)a) - *((const s8*)b);
}

static inline int board_sensors_count(enum board board)
{
	return __builtin_popcount(known_board_sensors[board]);
}

static void __init setup_sensor_data(struct ec_sensors_data *ec, board_t board)
{
	const int board_sensors = known_board_sensors[board];
	struct ec_sensor *s = ec->sensors;
	bool bank_found;
	int i, j;
	u8 bank;

	ec->nr_banks = 0;
	ec->nr_registers = 0;

	for (i = 1; i <= SENSOR_MAX; i <<= 1) {
		if ((i & board_sensors) == 0) continue;
		s->info_index = __builtin_ctz(i);
		s->cached_value = 0;
		ec->nr_registers +=
			known_ec_sensors[s->info_index].addr.components.size;
		bank_found = false;
		bank = known_ec_sensors[s->info_index].addr.components.bank;
		for (j = 0; j < ec->nr_banks; j++) {
			if (ec->banks[j] == bank) {
				bank_found = true;
				break;
			}
		}
		if (!bank_found) {
			ec->banks[ec->nr_banks++] = bank;
		}
		s++;
	}
	sort(ec->banks, ec->nr_banks, 1, &bank_compare, NULL);
}

static void __init fill_ec_registers(struct ec_sensors_data *ec)
{
	const struct ec_sensor_info *si;
	unsigned i, j, register_idx = 0;
	for (i = 0; i < ec->nr_sensors; ++i) {
		si = get_sensor_info(ec, i);
		for (j = 0; j < si->addr.components.size; ++j, ++register_idx) {
			ec->registers[register_idx] =
				(si->addr.components.bank << 8) +
				si->addr.components.index + j;
		}
	}
}

static int get_version(u32 *version)
{
	/* we know only a single version so far */
	*version = 0;
	return 0;
}

static acpi_handle asus_hw_access_mutex(struct device *dev)
{
	acpi_handle res;
	acpi_status status = acpi_get_handle(NULL, ASUS_HW_ACCESS_MUTEX_NAME, &res);
	if (ACPI_FAILURE(status)) {
		dev_err(dev, "Could not get hardware access guard mutex: error %d", status);
		return NULL;
	}
	return res;
}

/*
 * Switches ASUS EC banks.
 */
static int asus_ec_bank_switch(u8 bank, u8 *old)
{
	int status = 0;
	if (old) {
		status = ec_read(ASUS_EC_BANK_REGISTER, old);
	}
	if (status || (old && (*old == bank))) return status;
	return ec_write(ASUS_EC_BANK_REGISTER, bank);
}

static int asus_ec_block_read(const struct device *dev,
			     struct ec_sensors_data *ec)
{
	int ireg, ibank, status;
	u8 bank, reg_bank, prev_bank;

	bank = 0;
	status = asus_ec_bank_switch(bank, &prev_bank);
	if (status) {
		dev_warn(dev, "EC bank switch failed");
		return status;
	}

	if (prev_bank) {
		/* oops... somebody else is working with the EC too */
		dev_warn(dev, "Concurrent access to the ACPI EC "
			"detected.\nRace condition possible.");
	}

	/*
	 * read registers minimizing bank switches.
	 */
	for (ibank = 0; ibank < ec->nr_banks; ibank++) {
		if (bank != ec->banks[ibank]) {
			bank = ec->banks[ibank];
			status = asus_ec_bank_switch(bank, NULL);
			if (status) {
				dev_warn(dev, "EC bank switch to %d failed", bank);
				break;
			}
		}
		for (ireg = 0; ireg < ec->nr_registers; ireg++) {
			reg_bank = register_bank(ec->registers[ireg]);
			if (reg_bank < bank) {
				continue;
			}
			ec_read(ec->registers[ireg] & 0x00ff, ec->read_buffer + ireg);
		}
	}

	status = asus_ec_bank_switch(prev_bank, NULL);
	return status;
}

static inline u32 get_sensor_value(const struct ec_sensor_info *si, u8 *data)
{
	switch (si->addr.components.size) {
	case 1:
		return *data;
	case 2:
		return get_unaligned_be16(data);
	case 4:
		return get_unaligned_be32(data);
	default:
		return 0;
	}
}

static void update_sensor_values(struct ec_sensors_data *ec, u8 *data)
{
	const struct ec_sensor_info *si;
	struct ec_sensor *s;

	for (s = ec->sensors; s != ec->sensors + ec->nr_sensors; s++) {
		si = &known_ec_sensors[s->info_index];
		s->cached_value = get_sensor_value(si, data);
		data += si->addr.components.size;
	}
}

static int update_ec_sensors(const struct device *dev,
			     struct ec_sensors_data *ec)
{
	int status;

	/*
	 * ASUS DSDT does not specify that access to the EC has to be guarded,
	 * but firmware does access it via ACPI
	 */
	if (ACPI_FAILURE(acpi_acquire_mutex(ec->aml_mutex, NULL, ACPI_DELAY_MSEC_LOCK))) {
		dev_err(dev, "Failed to acquire AML mutex");
		status = -EBUSY;
		goto cleanup;
	}

	status = asus_ec_block_read(dev, ec);

	if (!status) {
		update_sensor_values(ec, ec->read_buffer);
	}
	if (ACPI_FAILURE(acpi_release_mutex(ec->aml_mutex, NULL))) {
		dev_err(dev, "Failed to release AML mutex");
	}
cleanup:
	return status;
}

static int scale_sensor_value(u32 value, int data_type)
{
	switch (data_type) {
	case hwmon_curr:
	case hwmon_temp:
	case hwmon_in:
		return value * 1000;
	default:
		return value;
	}
}

static int get_cached_value_or_update(const struct device *dev,
				      int sensor_index,
				      struct ec_sensors_data *state, u32 *value)
{
	if (time_after(jiffies, state->last_updated + HZ)) {
		if (update_ec_sensors(dev, state)) {
			dev_err(dev, "update_ec_sensors() failure\n");
			return -EIO;
		}

		state->last_updated = jiffies;
	}

	*value = state->sensors[sensor_index].cached_value;
	return 0;
}

/*
 * Now follow the functions that implement the hwmon interface
 */

static int asus_wmi_hwmon_read(struct device *dev, enum hwmon_sensor_types type,
			       u32 attr, int channel, long *val)
{
	int ret;
	u32 value = 0;

	struct ec_sensors_data *state = get_sensor_data(dev);
	int sidx = find_ec_sensor_index(state, type, channel);
	if (sidx < 0) {
		return sidx;
	}

	ret = get_cached_value_or_update(dev, sidx, state, &value);
	if (!ret) {
		*val = scale_sensor_value(value,
					  get_sensor_info(state, sidx)->type);
	}

	return ret;
}

static int asus_wmi_hwmon_read_string(struct device *dev,
				      enum hwmon_sensor_types type, u32 attr,
				      int channel, const char **str)
{
	struct ec_sensors_data *state = get_sensor_data(dev);
	int sensor_index = find_ec_sensor_index(state, type, channel);
	*str = get_sensor_info(state, sensor_index)->label;

	return 0;
}

static umode_t asus_wmi_hwmon_is_visible(const void *drvdata,
					 enum hwmon_sensor_types type, u32 attr,
					 int channel)
{
	const struct asus_ec_sensors *state = drvdata;
	return find_ec_sensor_index(&state->sensors_data, type, channel) >= 0 ?
			     S_IRUGO :
			     0;
}

static int
asus_wmi_hwmon_add_chan_info(struct hwmon_channel_info *asus_wmi_hwmon_chan,
			     struct device *dev, int num,
			     enum hwmon_sensor_types type, u32 config)
{
	int i;
	u32 *cfg = devm_kcalloc(dev, num + 1, sizeof(*cfg), GFP_KERNEL);

	if (!cfg)
		return -ENOMEM;

	asus_wmi_hwmon_chan->type = type;
	asus_wmi_hwmon_chan->config = cfg;
	for (i = 0; i < num; i++, cfg++)
		*cfg = config;

	return 0;
}

static const struct hwmon_ops asus_wmi_hwmon_ops = {
	.is_visible = asus_wmi_hwmon_is_visible,
	.read = asus_wmi_hwmon_read,
	.read_string = asus_wmi_hwmon_read_string,
};

static struct hwmon_chip_info asus_wmi_chip_info = {
	.ops = &asus_wmi_hwmon_ops,
	.info = NULL,
};

static int __init supported_board_index(const struct device *dev)
{
	const struct dmi_system_id *dmi_entry;
	u32 version = 0;

	dmi_entry = dmi_first_match(asus_ec_dmi_table);
	if (!dmi_entry) {
		dev_info(dev, "Unsupported board");
		return -ENODEV;
	}

	if (get_version(&version)) {
		dev_err(dev, "Error getting version");
		return -ENODEV;
	}

	return dmi_entry - asus_ec_dmi_table;
}

static int __init configure_sensor_setup(struct platform_device *pdev)
{
	struct asus_ec_sensors *asus_ec_sensors = platform_get_drvdata(pdev);
	struct ec_sensors_data *ec_data = &asus_ec_sensors->sensors_data;
	int nr_count[hwmon_max] = { 0 }, nr_types = 0;
	struct device *hwdev;
	struct device *dev = &pdev->dev;
	struct hwmon_channel_info *asus_wmi_hwmon_chan;
	const struct hwmon_channel_info **ptr_asus_wmi_ci;
	const struct hwmon_chip_info *chip_info;
	const struct ec_sensor_info *si;
	enum hwmon_sensor_types type;
	unsigned i;

	asus_ec_sensors->board = supported_board_index(dev);
	if (asus_ec_sensors->board < 0) {
		return -ENODEV;
	}

	ec_data->nr_sensors = board_sensors_count(asus_ec_sensors->board);
	ec_data->sensors = devm_kcalloc(dev, ec_data->nr_sensors,
					sizeof(struct ec_sensor), GFP_KERNEL);

	setup_sensor_data(ec_data, asus_ec_sensors->board);
	ec_data->registers = devm_kcalloc(dev, ec_data->nr_registers,
		sizeof(u16), GFP_KERNEL);
	ec_data->read_buffer = devm_kcalloc(dev, ec_data->nr_registers,
		sizeof(u8), GFP_KERNEL);

	if (!ec_data->registers || !ec_data->read_buffer) {
		return -ENOMEM;
	}

	fill_ec_registers(ec_data);

	ec_data->aml_mutex = asus_hw_access_mutex(dev);

	for (i = 0; i < asus_ec_sensors->sensors_data.nr_sensors; ++i) {
		si = get_sensor_info(&asus_ec_sensors->sensors_data, i);
		if (!nr_count[si->type])
			++nr_types;
		++nr_count[si->type];
	}

	if (nr_count[hwmon_temp])
		nr_count[hwmon_chip]++, nr_types++;

	asus_wmi_hwmon_chan = devm_kcalloc(
		dev, nr_types, sizeof(*asus_wmi_hwmon_chan), GFP_KERNEL);
	if (!asus_wmi_hwmon_chan)
		return -ENOMEM;

	ptr_asus_wmi_ci = devm_kcalloc(dev, nr_types + 1,
				       sizeof(*ptr_asus_wmi_ci), GFP_KERNEL);
	if (!ptr_asus_wmi_ci)
		return -ENOMEM;

	asus_wmi_chip_info.info = ptr_asus_wmi_ci;
	chip_info = &asus_wmi_chip_info;

	for (type = 0; type < hwmon_max; ++type) {
		if (!nr_count[type])
			continue;

		asus_wmi_hwmon_add_chan_info(asus_wmi_hwmon_chan, dev,
					     nr_count[type], type,
					     hwmon_attributes[type]);
		*ptr_asus_wmi_ci++ = asus_wmi_hwmon_chan++;
	}

	dev_info(dev, "board has %d EC sensors that span %d registers",
		 asus_ec_sensors->sensors_data.nr_sensors,
		 asus_ec_sensors->sensors_data.nr_registers);

	hwdev = devm_hwmon_device_register_with_info(
		dev, "asus-ec-sensors", asus_ec_sensors, chip_info, NULL);

	return PTR_ERR_OR_ZERO(hwdev);
}

static struct platform_device *asus_ec_sensors_platform_device;

static int asus_ec_probe(struct platform_device *pdev)
{
	struct asus_ec_sensors *state = platform_get_drvdata(pdev);

	if (state->board < 0) {
		return -ENODEV;
	}

	return 0;
}

static struct platform_driver asus_ec_sensors_platform_driver = {
	.driver = {
		.name	= "asus-ec-sensors",
	},
	.probe		= asus_ec_probe
};

MODULE_DEVICE_TABLE(dmi, asus_ec_dmi_table);

static void cleanup_device(void)
{
	platform_device_unregister(asus_ec_sensors_platform_device);
	platform_driver_unregister(&asus_ec_sensors_platform_driver);
}

static int __init asus_ec_init(void)
{
	struct asus_ec_sensors *state;
	int status = 0;

	asus_ec_sensors_platform_device =
		platform_create_bundle(&asus_ec_sensors_platform_driver,
				       asus_ec_probe, NULL, 0, NULL, 0);

	if (IS_ERR(asus_ec_sensors_platform_device))
		return PTR_ERR(asus_ec_sensors_platform_device);

	state = devm_kzalloc(&asus_ec_sensors_platform_device->dev,
			     sizeof(struct asus_ec_sensors), GFP_KERNEL);

	if (!state) {
		status = -ENOMEM;
		goto cleanup;
	}

	platform_set_drvdata(asus_ec_sensors_platform_device, state);
	status = configure_sensor_setup(asus_ec_sensors_platform_device);
cleanup:
	if (status) {
		cleanup_device();
	}
	return status;
}

static void __exit asus_ec_exit(void)
{
	cleanup_device();
}

module_init(asus_ec_init);
module_exit(asus_ec_exit);

MODULE_AUTHOR("Eugene Shalygin <eugene.shalygin@gmail.com>");
MODULE_DESCRIPTION(
	"HWMON driver for sensors accessible via EC in ASUS motherboards");
MODULE_LICENSE("GPL");
MODULE_VERSION("1");

// kate: tab-width 8; indent-width 8;
