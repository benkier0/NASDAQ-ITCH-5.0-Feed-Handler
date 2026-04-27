#!/usr/bin/env bash
set -euo pipefail

NIC=${1:?Usage: nic_tune.sh <interface>}

ethtool -C "$NIC" rx-usecs 0 tx-usecs 0
ethtool -G "$NIC" rx 4096
ethtool -K "$NIC" gro off gso off tso off

echo "NIC $NIC tuned: coalescing disabled, ring=4096, offloads off"
