## sip-lab

### Overview

A nodejs module that helps to write functional tests for SIP systems (including media operations).
It uses pjproject for SIP and media processing.

It permits to:
  - make audio calls using UDP, TCP and TLS transports
  - send and receive DTMF inband/RFC2833/INFO.
  - play/record wav file on a call
  - send/receive fax (T.30 only)

TODO:
  - add suport for T.38 fax
  - add support for WebRTC
  - add support for video playing/recording from/to file

### Installation

This is an node.js addon and it is known to work on Ubuntu 18.04, Ubuntu 20.04, Ubuntu 22.04, Debian 10 and Debian 11 (it is known to not work on Debian 8 unless the app is built locally).
It is distributed with prebuild binaries for node.js 15.0.0 and above.

To install it, just do:
```
npm install sip-lab
```

If a prebuilt binary is not available, npm will try to build the addon.

This might fail if you don't have the required tools and libraries.

If this happens try installing them by doing:
```
apt install build-essential automake autoconf libtool libspeex-dev libopus-dev libsdl2-dev libavdevice-dev libswscale-dev libv4l-dev libopencore-amrnb-dev libopencore-amrwb-dev libvo-amrwbenc-dev libopus-dev libsdl2-dev libopencore-amrnb-dev libopencore-amrwb-dev libvo-amrwbenc-dev libboost-dev libtiff-dev libpcap-dev libssl-dev uuid-dev cmake
```

To test from within this repo you will need to download and build dependencies. Do:
```
./build_deps.sh
```

Then build the node addon by doing:
```
  npm install
```

And run some sample script from subfolder samples:
```
  node samples/simple.js
```

The above script has detailed comments. 

Please read it to undestand how to write your own test scripts.

### About the code

Although the code in written in *.cpp/*.hpp named files, this is not actually a C++ project.

It is mostly written in C using some C++ facilities.


