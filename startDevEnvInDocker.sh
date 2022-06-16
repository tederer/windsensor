#!/bin/bash

scriptDir=$(cd $(dirname $0) && pwd)

projectRootInHost=$scriptDir
projectRootInContainer=/root/esp/windsensor/
deviceForFlashing=/dev/ttyUSB0

if [ -e $deviceForFlashing ]; then
   device=--device=$deviceForFlashing
else
   device=""
   echo
   echo "WARNING: $deviceForFlashing does not exist -> starting development environment but flashing will not be possible!"
   echo
fi

echo "project root on host: $scriptDir"

sudo docker run -it --rm --env="PROJECT_ROOT=$projectRootInContainer" --volume=$projectRootInHost:$projectRootInContainer:rw $device tederer/esp32dev
