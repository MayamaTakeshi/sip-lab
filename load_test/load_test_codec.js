const sip = require ('../index.js')
const Zeq = require('@mayama/zeq')
const m = require('data-matching')
const sip_msg = require('sip-matching')
const _ = require('lodash')

function usage() {
    console.log(`
Usage:
Parameters: codec_name wav_file"

Details:
    - codec_name: pcmu, pcma, g729, gsm, opus
    - wav_file: a wav file to be played during the call
`)
}

if(process.argv.length != 4) {
    usage()
    process.exit(1)
}

var codec_name = process.argv[2]
var codec

if (codec_name == 'opus') {
    codec = "opus/48000/2"
} else {
    codec = `${codec_name}/8000/1`
}

sip.set_codecs(`${codec}:128`)

const NUMBER_OF_CALLS = 100

var z = new Zeq()

sip.dtmf_aggregation_on(500)

z.add_event_filter({
    event: 'response',
    msg: sip_msg({
        $rs: '100',
    }),
})

async function test() {
    z.trap_events(sip.event_source, 'event', (evt) => {
        var e = evt.args[0]
        return e
    })

    console.log(sip.start((data) => { console.log(data)} ))

    const caller_ts = []
    const callee_ts = []

    const ocs = []

    for(var i=0 ; i<NUMBER_OF_CALLS ; i++) {   
        const caller_t = sip.transport.create({address: "127.0.0.1"})
        caller_ts.push(caller_t)
        const callee_t = sip.transport.create({address: "127.0.0.1"})
        callee_ts.push(callee_t)

        // make the call 
        const oc = sip.call.create(caller_t.id, {from_uri: 'sip:alice@test.com', to_uri: `sip:bob@${callee_t.address}:${callee_t.port}`})
        ocs.push(oc)
    }

    // Here we will wait for the calls to arrive
    await z.wait(_.chain(callee_ts).map(t => ({
        event: "incoming_call",
        call_id: m.push("ic_ids"),
        transport_id: t.id,
    })).value(), 50000)

    // Now we answer the calls
    z.store.ic_ids.forEach(ic_id => { 
        sip.call.respond(ic_id, {code: 200, reason: 'OK'})
    })

    // Then we wait for the '200 OK' at the caller side
    var events = _.chain(ocs).map(oc => ({
        event: 'response',
        call_id: oc.id,
        method: 'INVITE',
        msg: sip_msg({
            $rs: '200',
            $rr: 'OK',
        }),
    })).value()

    events = events.concat(_.chain(ocs).map(oc => ({
        event: 'media_update',
        call_id: oc.id,
        status: 'ok',
    })).value())
    
    events = events.concat(_.chain(z.store.ic_ids).map(ic_id => ({
        event: 'media_update',
        call_id: ic_id,
        status: 'ok',
    })).value())

    await z.wait(events, 50000)

    ocs.forEach(oc => { 
        sip.call.start_play_wav(oc.id, {file: 'StarWars60.pcmu.wav'})
    })

    z.store.ic_ids.forEach(ic_id => { 
        sip.call.start_play_wav(ic_id, {file: 'StarWars60.pcmu.wav'})
    })

    await z.sleep(60 * 1000)

    ocs.forEach(oc => {
        sip.call.terminate(oc.id)
    })

    // and wait for termination events
    events = _.chain(ocs).map(oc => ({
        event: 'response',
        call_id: oc.id,
        method: 'BYE',
        msg: sip_msg({
            $rs: '200',
            $rr: 'OK',
        }),
    })).value()

    events = events.concat(_.chain(ocs).map(oc => ({
        event: 'call_ended',
        call_id: oc.id,
    })).value())

    events = events.concat(_.chain(z.store.ic_ids).map(ic_id => ({
        event: 'call_ended',
        call_id: ic_id,
    })).value())

    await z.wait(events, 50000)

    console.log("Success")

    sip.stop()
    process.exit(0)
}


test()
.catch(e => {
    console.error(e)
    process.exit(1)
})

