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

This will require you to have some libraries installed. So do:
```
apt install build-essential automake autoconf libtool libspeex-dev libopus-dev libsdl2-dev libavdevice-dev libswscale-dev libv4l-dev libopencore-amrnb-dev libopencore-amrwb-dev libvo-amrwbenc-dev libopus-dev libsdl2-dev libopencore-amrnb-dev libopencore-amrwb-dev libvo-amrwbenc-dev libboost-dev libtiff-dev libpcap-dev libssl-dev uuid-dev
```

Then install sip-lab by doing:
```
npm install sip-lab
```

Be patient because we will need to download pjproject and build it.

We will also download and build spandsp, bcg729 and rapidjson.

However since it takes several minutes to build this module, you can install it globally:
```
npm install -g sip-lab
```

To test from within this repo just build and install by doing:
```
  npm install -g node-gyp
  npm install
```
And run some sample script from subfolder samples:
```
  node samples/simple.js
```
The above script has detailed comments. 
Please read it to undestand how to write your own test scripts.

The module is known to work properly in Ubuntu 18.04.4, Ubuntu 20.04.4, Debian 8 and Debian 10 (and it is expected to work in Debian 9).
It was originally developed with node v.10 and tested with v.12 and v16.13.1 and it is expected to work with latest versions of node.
(it is known to not work with node v.8)



### About the code

Although the code in written in *.cpp/*.hpp named files, this is not actually a C++ project.

It is mostly written in C using some C++ facilities.

