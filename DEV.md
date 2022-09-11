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


