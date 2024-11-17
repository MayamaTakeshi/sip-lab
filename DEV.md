### For devs

We build and statically link to libs pjproject, spandsp, bgc729 and rapidjson.

Basic tasks for development:

#### To build
```
sudo apt install build-essential automake autoconf libtool libspeex-dev libopus-dev libsdl2-dev libavdevice-dev libswscale-dev libv4l-dev libopencore-amrnb-dev libopencore-amrwb-dev libvo-amrwbenc-dev libvo-amrwbenc-dev libboost-dev libtiff-dev libpcap-dev libssl-dev uuid-dev cmake

npm install
```

Thne confirm it is working:
```
node samples/simple.js
```

#### To clean up (for a clean rebuild)
```
npx node-gyp clean
```

### To build the addon after changes in source files
```
npm run build
```
or force a full rebuild
```
npm run rebuild
```

#### To update pjproject, spandsp, bcg729 or rapidjson
Just delete the corresponding library subfolder in subfolder 3drParty.


Then temporarily change build_deps.sh to not checkout a specific version (or checkout a desired commit)

Then run
```
npm install
```

Then perform code changes and tests. When you are satisfied with them, update build_deps.sh with the new commit IDs.

#### prebuild binaries
Previously we would do:

```
nvm use v16.13.1 # if we try with v17 it will fail to build for -t 15.0.0
npx prebuildify --strip -t 15.0.0 -t 16.0.0 -t 17.0.0 -t 18.0.0 19.0.0 20.0.0 21.0.0
```
However the above will build the addon to run on the current OS.

Instead we will force the build on debian11 (using docker) using prebuildify-cross. So do this instead:

Make sure you have the docker image built (the image must be rebuilt whenever we update build_deps.sh)

cd docker-images/debian11/
./build_image.sh

If it fails due to proxy problems, check if you have proxy configured in ~/.docker/config.json like this:
```
{
    "proxies": {
        "default": {
            "httpProxy": "http://192.168.67.50:3128",
            "httpsProxy": "http://192.168.67.50:3128",
            "noProxy": "*.test.example.com,.example.org,127.0.0.0/8"
        }
    }
}

```

After the message is built you can pass them to prebuildify-cross:
```
nvm use v16.13.1
npx prebuildify-cross -i mayamatakeshi/sip-lab-debian11:latest -t 15.0.0 -t 16.0.0 -t 17.0.0 -t 18.0.0 -t 19.0.0 -t 20.0.0 -t 21.0.0 --strip
```
Obs: however the above will fail if you are behind proxy (solution pending).

#### Checking build using docker container

A quick check of the build can be done this way:
```
docker run -it debian:bookworm-slim /bin/bash

# then inside the container

apt update
apt install curl
curl -o- https://raw.githubusercontent.com/nvm-sh/nvm/v0.39.7/install.sh | bash
. ~/.nvm/nvm.sh
nvm i 19
mkdir -p /root/tmp/t1
cd /root/tmp/t1
npm init -y
apt install -y build-essential automake autoconf libtool libspeex-dev libopus-dev libsdl2-dev libavdevice-dev libswscale-dev libv4l-dev libopencore-amrnb-dev libopencore-amrwb-dev libvo-amrwbenc-dev libvo-amrwbenc-dev libboost-dev libtiff-dev libpcap-dev libssl-dev uuid-dev flite-dev cmake git wget
npm i sip-lab
```

Sample build:
```
root@636c5c5b0748:~/tmp/t2# nvm use 19
Now using node v19.9.0 (npm v9.6.3)

root@636c5c5b0748:~/tmp/t2# time npm i sip-lab
npm WARN deprecated inflight@1.0.6: This module is not supported, and leaks memory. Do not use it. Check out lru-cache if you want a good and tested way to coalesce async requests by a key value, which is much more comprehensive and powerful.
npm WARN deprecated @npmcli/move-file@2.0.1: This functionality has been moved to @npmcli/fs
npm WARN deprecated npmlog@6.0.2: This package is no longer supported.
npm WARN deprecated rimraf@3.0.2: Rimraf versions prior to v4 are no longer supported
npm WARN deprecated are-we-there-yet@3.0.1: This package is no longer supported.
npm WARN deprecated glob@7.2.3: Glob versions prior to v9 are no longer supported
npm WARN deprecated glob@8.1.0: Glob versions prior to v9 are no longer supported
npm WARN deprecated gauge@4.0.4: This package is no longer supported.

added 165 packages, and audited 166 packages in 6m

11 packages are looking for funding
  run `npm fund` for details

4 moderate severity vulnerabilities

To address all issues, run:
  npm audit fix

Run `npm audit` for details.

real	5m54.904s
user	4m32.643s
sys	0m54.272s

```

So it is taking aboult 6 minutes to build the addon on a docker container.

#### Running tests
```
npm test
```

#### publishing to npm registry

Do it from within the project folder. First do a dry-run to check which files will be included:
```
takeshi:sip-lab$ npm publish --dry-run
npm notice 
npm notice 📦  sip-lab@1.12.26
npm notice === Tarball Contents === 
npm notice 1.8kB   README.md                               
npm notice 1.5kB   build_deps.sh                           
npm notice 2.8kB   index.js                                
npm notice 757B    package.json                            
npm notice 1.8MB   prebuilds/linux-x64/node.abi88.node     
npm notice 1.8MB   prebuilds/linux-x64/node.abi93.node     
npm notice 1.9MB   prebuilds/linux-x64/node.abi102.node    
npm notice 1.9MB   prebuilds/linux-x64/node.abi108.node    
npm notice 173.2kB samples/artifacts/marielle_presente.tiff
npm notice 18.4kB  samples/artifacts/sample.tiff           
npm notice 74.0kB  samples/artifacts/this-is-never-ok.tiff 
npm notice 19.8kB  samples/artifacts/yosemitesam.wav       
npm notice 6.8kB   samples/delayed_media.js                
npm notice 4.6kB   samples/g729.js                         
npm notice 4.2kB   samples/register_subscribe.js           
npm notice 6.8kB   samples/reinvite_and_dtmf.js            
npm notice 3.5kB   samples/send_and_receive_fax.js         
npm notice 5.0kB   samples/simple.js                       
npm notice 2.9kB   samples/sip_cancel.js                   
npm notice 7.9kB   samples/tcp_and_extra_headers.js        
npm notice === Tarball Details === 
npm notice name:          sip-lab                                 
npm notice version:       1.12.26                                 
npm notice filename:      sip-lab-1.12.26.tgz                     
npm notice package size:  3.2 MB                                  
npm notice unpacked size: 7.7 MB                                  
npm notice shasum:        aa225a9a6c704eddbfb945fe1570184d881c12d2
npm notice integrity:     sha512-WtPrIFrFXlSAC[...]tLGc4T5h8y2qw==
npm notice total files:   20                                      
npm notice 
npm notice Publishing to https://registry.npmjs.org/ (dry-run)
+ sip-lab@1.12.26
takeshi:sip-lab$
```

Then do the actual publish:
```
npm publish 
```


