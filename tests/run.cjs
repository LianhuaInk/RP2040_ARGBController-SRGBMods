const fs = require('node:fs');
const path = require('node:path');
const {spawnSync} = require('node:child_process');
const root = path.resolve(__dirname, '..');
const scratch = path.join(root, 'work');
fs.mkdirSync(scratch, {recursive:true});
function run(command,args) {
  const result = spawnSync(command,args,{cwd:scratch,stdio:'inherit'});
  if(result.error) throw result.error;
  if(result.status !== 0) throw new Error(command+' failed: '+result.status);
}
const fixture = path.join(scratch,'cdc-wire.bin');
run(process.execPath,[path.join(__dirname,'cdc.test.cjs'),fixture]);
const source = path.join(__dirname,'cdc.test.cpp');
const include = path.join(__dirname,'stubs');
const binary = path.join(scratch,process.platform==='win32'?'cdc-test.exe':'cdc-test');
if(process.platform==='win32') {
  run('cl',['/nologo','/EHsc','/std:c++17','/W4','/WX','/D_CRT_SECURE_NO_WARNINGS',
    '/I'+include,'/Fe:'+binary,'/Fo:'+path.join(scratch,'cdc-test.obj'),source]);
} else {
  run('c++',['-std=c++17','-Wall','-Wextra','-Werror','-I'+include,source,'-o',binary]);
}
run(binary,[fixture]);
