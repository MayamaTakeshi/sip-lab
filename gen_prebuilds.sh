#!/bin/bash

set -o errexit
set -o nounset
set -o pipefail

# Remove pjproject entirely before running Docker so build_deps.sh fully
# recompiles it inside the container using Debian 11's OpenSSL 1.1 headers.
# If pjproject was previously built on the host (which has OpenSSL 3), its srtp
# static lib references EVP_MAC_fetch (an OpenSSL 3-only symbol). The Docker
# container (Debian 11) only has libssl.so.1.1 which lacks that symbol, causing
# "undefined symbol: EVP_MAC_fetch" at runtime.
echo "Removing pjproject to force full rebuild inside Docker with OpenSSL 1.1..."
rm -rf 3rdParty/pjproject

#docker run --rm   -v "$(pwd)":/project   -w /project   mayamatakeshi/sip-lab-debian11:latest   npx prebuildify -t 15.0.0 -t 16.0.0 -t 17.0.0 -t 18.0.0 -t 19.0.0 -t 20.0.0 -t 21.0.0 --strip

# we need to set target to an old version to get original NAPI (it will work with newer versions)
docker run --rm   -v "$(pwd)":/project   -w /project   mayamatakeshi/sip-lab-debian11:latest   npx prebuildify --strip --napi --target 18.0.0

# After Docker rebuilds pjproject with OpenSSL 1.1, rebuild locally for development.
# node-gyp rebuild (instead of build) ensures a clean state regardless of prior builds.
echo "Rebuilding locally for development..."
npm run rebuild

echo "success"
