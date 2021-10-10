#!/bin/bash

set -o errexit
set -o nounset
set -o pipefail

if [[ ! -d bcg729 ]]
then
    git clone https://github.com/MayamaTakeshi/bcg729
    cd bcg729
    git checkout faaa895862165acde6df8add722ba4f85a25007d
    cmake . 
    make
    sudo make install
    sudo ldconfig
fi

echo success
