### For devs

We build and statically link to libs pjproject, spandsp, bgc729 and rapidjson.

Basic tasks for development:

#### To build
```
sudo apt install build-essential automake autoconf libtool libspeex-dev libopus-dev libsdl2-dev libavdevice-dev libswscale-dev libv4l-dev libopencore-amrnb-dev libopencore-amrwb-dev libvo-amrwbenc-dev libvo-amrwbenc-dev libboost-dev libtiff-dev libpcap-dev libssl-dev uuid-dev cmake flite-dev

make

Then confirm it is working:
```
node samples/simple.js
```
#### Running tests
```
npm test

#### publishing to npm registry
```
npm publish 
```
```
If you get something like
```
npm timing command:publish Completed in 8775ms
npm ERR! code E404
npm ERR! 404 Not Found - PUT https://registry.npmjs.org/sip-lab - Not found
npm ERR! 404 
npm ERR! 404  'sip-lab@1.45.0' is not in this registry.

```
it means you are not logged in. 
So first do:
```
npm login
```


