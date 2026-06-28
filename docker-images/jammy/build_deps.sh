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
    CFLAGS="-O -fPIC" ./configure --enable-shared
    make
'

ensure_git_repo rapidjson https://github.com/Tencent/rapidjson 27c3a8dc0e2c9218fe94986d249a12b5ed838f1d

ensure_git_repo bcg729 https://github.com/MayamaTakeshi/bcg729 faaa895862165acde6df8add722ba4f85a25007d '
    cmake .
    make
    mkdir -p lib
    cp -f src/libbcg729.a lib
'

ensure_git_repo pjproject https://github.com/pjsip/pjproject 9543a1bcf50be721d030be99afeeb63bd8cf2013 '
    cat > user.mak <<EOF
    export CFLAGS += -fPIC -g
    export LDFLAGS +=
EOF
    sed -i -r "s/BCG729_LIBS=\"-lbcg729\"/BCG729_LIBS=\"\"/" aconfigure
    LIBS=$(pwd)/../bcg729/src/libbcg729.a ./configure --with-bcg729=$(pwd)/../bcg729
    cat > pjlib/include/pj/config_site.h <<EOF2
#define PJSUA_MAX_ACC (20000)
#define PJ_IOQUEUE_MAX_HANDLES (1024)
#define PJSUA_MAX_CALLS (20000)
EOF2
    make dep && make clean && make
'

cd $START_DIR/3rdParty
if [[ ! -d boost_1_51_0 ]]
then
    wget http://sourceforge.net/projects/boost/files/boost/1.51.0/boost_1_51_0.tar.bz2
    tar xf boost_1_51_0.tar.bz2
fi


echo "Build of dependencies successful"
