#!/bin/bash

set -o errexit
set -o nounset
set -o pipefail


START_DIR=`pwd`


mkdir -p $START_DIR/3rdParty


cd $START_DIR/3rdParty
if [[ ! -d spandsp ]]
then
	commit=e59ca8fb8b1591e626e6a12fdc60a2ebe83435ed
	git clone https://github.com/freeswitch/spandsp
	cd spandsp
	git checkout $commit
	./bootstrap.sh
	CFLAGS='-O -fPIC' ./configure --enable-shared
    make
fi


cd $START_DIR/3rdParty
if [[ ! -d rapidjson ]]
then
	git clone https://github.com/Tencent/rapidjson
	cd rapidjson
	git checkout 27c3a8dc0e2c9218fe94986d249a12b5ed838f1d
fi


cd $START_DIR/3rdParty
if [[ ! -d bcg729 ]]
then
    git clone https://github.com/MayamaTakeshi/bcg729
    cd bcg729
    git checkout faaa895862165acde6df8add722ba4f85a25007d
    cmake . 
    make
    mkdir -p lib
    cp -f src/libbcg729.a lib
fi


cd $START_DIR/3rdParty
if [[ ! -d pjproject ]]
then
	git clone https://github.com/pjsip/pjproject
	cd pjproject
	#git checkout de3d744c2e1188b59bb907b6ee32ef83740ebc64
    #git checkout 33a3c9e0a5eb84426edef05a9aa98af17d8011c3 # required for bcg729
    git checkout 797088ed133c98492519b7d042b75735f6f9388c # updated as part of #21

	cat > user.mak <<EOF
	export CFLAGS += -fPIC -g
	export LDFLAGS +=
EOF

    sed -i -r 's/BCG729_LIBS="-lbcg729"/BCG729_LIBS=''/' aconfigure
    LIBS=`pwd`/../bcg729/src/libbcg729.a ./configure --with-bcg729=`pwd`/../bcg729
	cat > pjlib/include/pj/config_site.h <<EOF
#define PJMEDIA_HAS_SRTP  0
EOF
	make dep && make clean && make
fi


echo "Build of dependencies successful"
