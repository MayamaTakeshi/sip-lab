## sip-lab

### Overview

A nodejs module that helps to write functional tests for SIP systems (including media operations).
It uses pjproject for SIP and media processing.

It permits to:
  - make audio calls using UDP, TCP and TLS transports
  - send/receive DTMF inband/RFC2833/INFO.
  - play/record audio on a call from/to a wav file
  - send/receive fax (T.30 only)
  - send/receive MRCPv2 messages (TCP only, no TLS)
  - send/receive audio using SRTP
  - do speech synth using flite
  - do speech recog using pocketsphinx (but only works well with sampling rate of 16000)
  - do speech synth/recog using [ws_speech_server](https://github.com/MayamaTakeshi/ws_speech_server) (this permits to use google/amazon/azure/etc speech services)

TODO:
  - add support for video playing/recording from/to file
  - add support for T.38 fax
  - add support for SIP over WebSocket
  - add support for WebRTC
  - add support for MSRP

### Installation

This is a node.js addon and it is known to work on Debian 11, Debian 10, Ubuntu 22.04 and Ubuntu 20.04.
It is distributed with prebuild binaries for node.js 15.0.0 and above (but built for Debian 11. For other Debian versions or for Ubuntu a local build of the addon will be executed. Being the case, be patient as the build process will take several minutes to complete). 

To install it, first install build dependencies:
```
apt install build-essential automake autoconf libtool libspeex-dev libopus-dev libsdl2-dev libavdevice-dev libswscale-dev libv4l-dev libopencore-amrnb-dev libopencore-amrwb-dev libvo-amrwbenc-dev libvo-amrwbenc-dev libboost-dev libtiff-dev libpcap-dev libssl-dev uuid-dev flite-dev cmake
```

Then install sip-lab (local build of the addon might be triggered here if this is not Debian 11):
```
npm install sip-lab
```

Then run some sample script from subfolder samples:
```
node node_modules/sip-lab/samples/simple.js
```

The above script has detailed comments. 

Please read it to undestand how to write your own test scripts.


### Samples

See general sample scripts in folder samples.

There are additional samples scripts in folder samples_extra but they require [ws_speech_server](https://github.com/MayamaTakeshi/ws_speech_server) to be running locally (and it should be started with GOOGLE_APPLICATION_CREDENTIALS set).

To run ws_speech_server, do this:
```
git clone https://github.com/MayamaTakeshi/ws_speech_server
cd ws_speech_server
npm i
npm run build
cp config/default.js.sample config/default.js
export GOOGLE_APPLICATION_CREDENTIALS=/path/to/your/credentials/file
node src/App.bs.js
```

Then you should be able to test with dtmf language:
```
node node_modules/sip-lab/samples_extra/ws_speech_server.dtmf.js
```
or with google speech service:
```
node node_modules/sip-lab/samples_extra/ws_speech_server.google.js
```


### About the code

Although the code in written in *.cpp/*.hpp named files, this is not actually a C++ project.

It is mostly written in C using some C++ facilities.


