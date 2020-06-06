const http = require("https")
const fs = require('fs');
const crypto = require('crypto');
const zlib = require('zlib');

const fetch = (url) => new Promise((resolve, reject) => {
    http.get(url, (res) => {
        let data = '';
        res.on('end', () => resolve(data));
        res.on('data', (buf) => data += buf.toString());
    })
        .on('error', e => reject(e));
});

async function main() {

    var s = await fetch("https://raw.githubusercontent.com/mstratman/fv1-programs/master/programs.js");
    eval(s.replace("export default", "var programs = "));

    var spn_programs = [];
    for (i = 0; i < programs.length; i++) {
        if (programs[i]["download"].hasOwnProperty('spn')) {
            programs[i]["download"]["spn"]["file"] = "https://mstratman.github.io/fv1-programs/files/" + programs[i]["download"]["spn"]["file"];

            var tmp = await fetch(programs[i]["download"]["spn"]["file"])
            const hash = crypto.createHash('sha256').update(tmp).digest('hex');
            console.log(hash)
            const buff = Buffer.from(tmp, 'utf-8');
            programs[i]["download"]["spn"]["sha256"] = hash;
            programs[i]["download"]["spn"]["base64"] = buff.toString("base64");
            spn_programs.push(programs[i]);

            //fs.writeFile(hash + '.spn', tmp, function (err) {
            //    if (err) return console.log(err);
            //});
        }
    }
    
    const programs_json = JSON.stringify(spn_programs, null, "\t");
    fs.writeFile('programs.json', programs_json, function (err) {
        if (err) return console.log(err);
    });

    zlib.gzip(programs_json, function (_, result) {  
        fs.writeFile('programs.json.gz', result, function (err) {
            if (err) return console.log(err);
        });
      });
}

main();
