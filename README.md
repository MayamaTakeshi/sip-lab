# sip-lab

## Overview

A nodejs module that helps to write functional/integration tests for SIP systems (including media operations).
It uses pjproject for SIP and media processing.

## Documentation

See [Documentation](https://github.com/MayamaTakeshi/sip-lab/blob/master/DOC.md)

## Installation

The npm package is built for Ubuntu/Debian and might work with other linux distros.

First install required apt packages:

```
apt install libspeex-dev libopus-dev libavdevice-dev libtiff-dev libssl-dev uuid-dev flite-dev
```

Then:
```
npm i sip-lab
```
Then run some sample script from subfolder samples:
```
cd node_modules/sip-lab
node ./samples/simple.js
```

The above script has detailed comments. 

Please read it to undestand how to write your own test scripts.

## Samples

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


## About the code

Although the code is written in *.cpp/*.hpp named files, this is not actually a C++ project.

It is mostly written in C using some C++ facilities.

## Release Notes

[ReleaseNotes](https://github.com/MayamaTakeshi/sip-lab/blob/master/RELEASE_NOTES.md)


