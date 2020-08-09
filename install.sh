#!/bin/bash

set -o errexit
set -o nounset
set -o pipefail

if [[ ! -d pjproject ]]
then
	git clone https://github.com/pjsip/pjproject
	cd pjproject
	git checkout de3d744c2e1188b59bb907b6ee32ef83740ebc64

	cat > user.mak <<EOF
	export CFLAGS += -fPIC -g
	export LDFLAGS +=
EOF

	./configure
	touch pjlib/include/pj/config_site.h

	make dep && make clean && make

	cd ..
fi

node-gyp configure

node-gyp build
