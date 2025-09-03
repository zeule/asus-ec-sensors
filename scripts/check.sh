#!/bin/bash

set -euo pipefail

echo "Checking order of ec_board_info structs"
grep 'static const struct ec_board_info board' asus-ec-sensors.c | LC_COLLATE=C sort -c

echo "Checking order of DMI_EXACT_MATCH_ASUS_BOARD_NAME"
grep -P '\tDMI_EXACT_MATCH_ASUS_BOARD_NAME' asus-ec-sensors.c | sed 's/ /_/g' | LC_COLLATE=C sort -c

echo "Checks passed."
