#!/bin/bash

set -o errexit
set -o nounset
set -o pipefail


START_DIR=$(pwd)


ensure_git_repo() {
    local dir=$1
    local repo=$2
    local commit=$3
    shift 3

    cd "$START_DIR/3rdParty"
    if [[ -d "$dir" ]]; then
        cd "$dir"
        local current=$(git rev-parse HEAD 2>/dev/null || echo "")
        cd "$START_DIR/3rdParty"
        if [[ "$current" == "$commit" ]]; then
            echo "$dir: already at desired commit $commit, skipping"
            return 0
        fi
        echo "$dir: commit mismatch ($current != $commit), re-cloning"
        rm -rf "$dir"
    fi

    git clone "$repo" "$dir"
    cd "$dir"
    git checkout "$commit"

    if [[ $# -gt 0 ]]; then
        eval "$*"
    fi
    cd "$START_DIR/3rdParty"
}


mkdir -p $START_DIR/3rdParty

ensure_git_repo spandsp https://github.com/freeswitch/spandsp e59ca8fb8b1591e626e6a12fdc60a2ebe83435ed '
    ./bootstrap.sh
    CFLAGS="-O -fPIC" ./configure --disable-shared --enable-static
    make
'

ensure_git_repo rapidjson https://github.com/Tencent/rapidjson 27c3a8dc0e2c9218fe94986d249a12b5ed838f1d

ensure_git_repo bcg729 https://github.com/MayamaTakeshi/bcg729 faaa895862165acde6df8add722ba4f85a25007d '
    cmake -DCMAKE_BUILD_TYPE=RelWithDebInfo .
    make
    mkdir -p lib
    cp -f src/libbcg729.a lib
'

cd $START_DIR/3rdParty
ALSA_VERSION=1.2.6.1
if [[ ! -d alsa-lib-${ALSA_VERSION} ]]; then
    wget -q https://www.alsa-project.org/files/pub/lib/alsa-lib-${ALSA_VERSION}.tar.bz2
    tar xf alsa-lib-${ALSA_VERSION}.tar.bz2
    rm -f alsa-lib-${ALSA_VERSION}.tar.bz2
fi
if [[ ! -f alsa-lib-${ALSA_VERSION}/src/.libs/libasound.a ]]; then
    cd alsa-lib-${ALSA_VERSION}
    ./configure --disable-shared --enable-static
    make
    cd $START_DIR/3rdParty
else
    echo "alsa-lib: already built, skipping"
fi

ensure_git_repo pjproject https://github.com/pjsip/pjproject 9543a1bcf50be721d030be99afeeb63bd8cf2013 '
    cat > user.mak <<EOF
    export CFLAGS += -fPIC -g
    export LDFLAGS +=
EOF
    sed -i -r "s/BCG729_LIBS=\"-lbcg729\"/BCG729_LIBS=\"\"/" aconfigure
    LIBS=$(pwd)/../bcg729/src/libbcg729.a ./configure --disable-shared --enable-static --with-bcg729=$(pwd)/../bcg729
    cat > pjlib/include/pj/config_site.h <<EOF2
#define PJSUA_MAX_ACC (20000)
#define PJ_IOQUEUE_MAX_HANDLES (1024)
#define PJSUA_MAX_CALLS (20000)

#define PJMEDIA_HAS_OPUS_CODEC 1
EOF2
    make dep && make clean && make
'

cd $START_DIR/3rdParty
if [[ ! -d boost_1_66_0 ]]
then
    wget https://downloads.sourceforge.net/project/boost/boost/1.66.0/boost_1_66_0.tar.bz2
    tar xf boost_1_66_0.tar.bz2
fi

cd $START_DIR/3rdParty
POCKETSPHINX_VERSION=5.0.3
if [[ -f pocketsphinx/.version && "$(cat pocketsphinx/.version)" != "$POCKETSPHINX_VERSION" ]]
then
    echo "pocketsphinx: version mismatch, re-downloading"
    rm -rf pocketsphinx
fi
if [[ ! -d pocketsphinx ]]
then
    rm -f v${POCKETSPHINX_VERSION}.tar.gz
    wget https://github.com/cmusphinx/pocketsphinx/archive/refs/tags/v${POCKETSPHINX_VERSION}.tar.gz
    tar xf v${POCKETSPHINX_VERSION}.tar.gz
    rm -f v${POCKETSPHINX_VERSION}.tar.gz
    mv pocketsphinx-${POCKETSPHINX_VERSION} pocketsphinx
    cd pocketsphinx
    sed -i '/include(GNUInstallDirs)/a \\nset(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fPIC")' CMakeLists.txt
    cmake -S . -B build
    cmake --build build
    echo "$POCKETSPHINX_VERSION" > .version

    rm -fr ../../pocketsphinx
    mkdir -p ../../pocketsphinx
    cp -rf model/ ../../pocketsphinx/
fi


ensure_git_repo pjwebsock https://github.com/jimying/pjwebsock ed8bfee79e26ef4e023bac1359301c201ee133af


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
