## sip-lab

Node module that helps to write functional tests for SIP systems (including media operations).
It uses pjproject for SIP and media processing.

This will require for you to have some libraries installed. So do:
```
apt install build-essential automake autoconf libtool libspeex-dev libopus-dev libsdl2-dev libavdevice-dev libswscale-dev libv4l-dev libopencore-amrnb-dev libopencore-amrwb-dev libvo-amrwbenc-dev libopus-dev libsdl2-dev libopencore-amrnb-dev libopencore-amrwb-dev libvo-amrwbenc-dev libboost-dev libspandsp-dev libpcap-dev
```

Then install sip-lab by doing:
```
  npm install sip-lab
```

However, since it takes a long time to fetch and build pjproject and the node addon for it, you could install sip-lab globally:
```
  npm install -g sip-lab
```

But if you do so, you will need to set NODE_PATH for node to find it by doing:
```
  export NODE_PATH=$(npm root --quiet -g)
```

To test from within this repo just run:
```
  npm install --unsafe-perm

  node samples/simple.js
```


The module is known to work properly in Ubuntu 18.04.4, Debian 8 and Debian 10 (and it is expected to work in Debian 9).
It works with node v.10.
It doesn't work with node v.8.


