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
```
nvm use v16.13.1 # if we try with v17 it will fail to build for -t 1.5.0.0
prebuildify --strip -t 15.0.0 -t 16.0.0 -t 17.0.0 -t 18.0.0
```


#### Running tests
```
npm test
```

