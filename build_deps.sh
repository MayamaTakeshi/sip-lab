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
    cmake -DCMAKE_BUILD_TYPE=RelWithDebInfo .
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
    #git checkout 797088ed133c98492519b7d042b75735f6f9388c # updated as part of #21
    #git checkout 651df5b50129b7c5a5feec8336dda4468d53d2b0 # updated to latest to see of crash issues improve
    #git checkout 043926a5846963a2c99378e8daa495230923eaab # updated to try to solve #49 (but issue remains)
    #git checkout c36802585ddefb3ca477d1f6d773d179510c5412 # updated to try to solve #83 (but issue remains)
    git checkout 9543a1bcf50be721d030be99afeeb63bd8cf2013 # updated to latest commit to permit to report https://github.com/pjsip/pjproject/issues/4082

    cat > user.mak <<EOF
    export CFLAGS += -fPIC -g
    export LDFLAGS +=
EOF

    sed -i -r 's/BCG729_LIBS="-lbcg729"/BCG729_LIBS=''/' aconfigure
    LIBS=`pwd`/../bcg729/src/libbcg729.a ./configure --with-bcg729=`pwd`/../bcg729
    cat > pjlib/include/pj/config_site.h <<EOF
#define PJSUA_MAX_ACC (20000)
#define PJ_IOQUEUE_MAX_HANDLES (1024)
#define PJSUA_MAX_CALLS (20000)
EOF
    make dep && make clean && make
fi


cd $START_DIR/3rdParty
if [[ ! -d boost_1_66_0 ]]
then
    wget https://downloads.sourceforge.net/project/boost/boost/1.66.0/boost_1_66_0.tar.bz2
    tar xf boost_1_66_0.tar.bz2
fi


cd $START_DIR/3rdParty
if [[ ! -d pocketsphinx ]]
then
    POCKETSPHINX_VERSION=5.0.3
    rm -f v${POCKETSPHINX_VERSION}.tar.gz
    wget https://github.com/cmusphinx/pocketsphinx/archive/refs/tags/v${POCKETSPHINX_VERSION}.tar.gz 
    tar xf v${POCKETSPHINX_VERSION}.tar.gz
    rm -f v${POCKETSPHINX_VERSION}.tar.gz
    mv pocketsphinx-${POCKETSPHINX_VERSION} pocketsphinx
    cd pocketsphinx
    sed -i '/include(GNUInstallDirs)/a \\nset(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fPIC")' CMakeLists.txt
    cmake -S . -B build
    cmake --build build

    rm -fr ../../pocketsphinx
    mkdir -p ../../pocketsphinx
    cp -rf model/ ../../pocketsphinx/
fi


cd $START_DIR/3rdParty
if [[ ! -d pjwebsock ]]
then
    git clone https://github.com/jimying/pjwebsock
    cd pjwebsock
    git checkout a0616ea27f01d5e3bdfd5b801fb1499473a0b0cb
fi


#cd $START_DIR/3rdParty
#if [[ ! -d openssl ]]
#then
#    git clone https://github.com/openssl/openssl
#    cd openssl
#    git checkout openssl-3.0.7
#    ./config -d # configure with debug symbols enabled
#    make
#fi
# statically linking to openssl will not solve anything (see #37).


echo "Build of dependencies successful"
