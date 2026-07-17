#!/bin/bash

set -o errexit
set -o nounset
set -o pipefail

npm run build

#docker run --rm   -v "$(pwd)":/project   -w /project   mayamatakeshi/sip-lab-debian11:latest   npx prebuildify -t 15.0.0 -t 16.0.0 -t 17.0.0 -t 18.0.0 -t 19.0.0 -t 20.0.0 -t 21.0.0 --strip

# we need to set target to an old version to get original NAPI (it will work with newer versions)
docker run --rm   -v "$(pwd)":/project   -w /project   mayamatakeshi/sip-lab-debian11:latest   npx prebuildify --strip --napi --target 18.0.0

echo "success"
