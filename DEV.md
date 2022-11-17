### For devs

We build and statically link to libs pjproject, spandsp, bgc729 and rapidjson.

Basic tasks for development:

#### To build
```
npm install
```

#### To clean up (for a clean rebuild)
```
npx node-gyp clean
```

#### To update pjproject, spandsp, bcg729 or rapidjson
Just delete the corresponding library subfolder in subfolder 3drParty.


Then temporarily change install.sh to not checkout a specific version (or checkout a desired commit)

Then run
```
npm install
```

Then perform code changes and tests. When you are satisfied with them, update install.sh with the new commit id.

#### prebuild binaries
Previously we would do:

```
nvm use v16.13.1 # if we try with v17 it will fail to build for -t 15.0.0
npx prebuildify --strip -t 15.0.0 -t 16.0.0 -t 17.0.0 -t 18.0.0
```
However the above will build the addon to run on the current OS.

Instead we will force the build on debian11 (using docker). So to this instead:
```
nvm use v16.13.1
npx prebuildify-cross -i mayamatakeshi/sip-lab-debian11:latest -t 15.0.0 -t 16.0.0 -t 17.0.0 -t 18.0.0 --strip
```

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


