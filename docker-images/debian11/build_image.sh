#!/bin/bash

set -o errexit
#set -o nounset
set -o pipefail

cp ../../build_deps.sh .

docker build --network=host --build-arg http_proxy=$http_proxy --build-arg https_proxy=$https_proxy --build-arg no_proxy=$no_proxy -t mayamatakeshi/sip-lab-debian11:latest . 

