#!/bin/bash

set -o errexit
set -o nounset
set -o pipefail

if [[ ! -d pjproject ]]
then
	git clone https://github.com/pjsip/pjproject
	cd pjproject
	git checkout de3d744c2e1188b59bb907b6ee32ef83740ebc64

    #echo "Patching sip_transaction.c to avoid problems with CANCEL"
    sed -i -r 's|event->body.tx_msg.tdata == tsx->last_tx,|\t\t\t1, /* \0 */|' pjsip/src/pjsip/sip_transaction.c 

	cat > user.mak <<EOF
	export CFLAGS += -fPIC -g
	export LDFLAGS +=
EOF

	./configure
	cat > pjlib/include/pj/config_site.h <<EOF
#define PJMEDIA_HAS_SRTP  0
EOF
	make dep && make clean && make

	cd ..
fi

node-gyp configure

node-gyp build
