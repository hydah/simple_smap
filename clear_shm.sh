#!/bin/bash

echo "clear exmaple shm..."
ipcs -m | grep 0x00302096 | awk '{print $2}'| xargs -I {} ipcrm -m {}