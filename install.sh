#!/bin/bash

set -o errexit
set -o nounset
set -o pipefail

npx node-gyp configure
npx node-gyp build
