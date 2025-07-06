## sip-lab

### Overview

A nodejs module that helps to write functional/integration tests for SIP systems (including media operations).
It uses pjproject for SIP and media processing.

### Documentation

See [Documentation](https://github.com/MayamaTakeshi/sip-lab/blob/master/DOC.md)

### Installation

The npm package is built for Debian 11 and this is the recommended distro.

You can use other debian/ubuntu version but they will require a build of dependencies that will take time (something like 7 minutes but this was measured on my slow PC).

First install apt packages:

```
apt install build-essential automake autoconf libtool libspeex-dev libopus-dev libsdl2-dev libavdevice-dev libswscale-dev libv4l-dev libopencore-amrnb-dev libopencore-amrwb-dev libvo-amrwbenc-dev libvo-amrwbenc-dev libboost-dev libtiff-dev libpcap-dev libssl-dev uuid-dev flite-dev cmake git wget

```

Then switch to node v19, switch to your node project folder and install sip-lab:

```
nvm install 19
nvm use 19
cd YOUR_NODE_PROJECT_FOLDER
npm i sip-lab
```
Obs: once you install sip-lab, you can switch to other node versions like v20, v21.


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

### Release Notes

[ReleaseNotes](https://github.com/MayamaTakeshi/sip-lab/blob/master/RELEASE_NOTES.md)


