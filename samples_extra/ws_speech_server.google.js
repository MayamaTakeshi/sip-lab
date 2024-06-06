var sip = require ('../index.js')
var Zeq = require('@mayama/zeq')
var z = new Zeq()
var m = require('data-matching')
var sip_msg = require('sip-matching')
var sdp = require('sdp-matching')

async function test() {
    //sip.set_log_level(6)
    sip.dtmf_aggregation_on(500)

    sip.set_codecs("PCMU/8000/1:128")

    z.trap_events(sip.event_source, 'event', (evt) => {
        var e = evt.args[0]
        return e
    })

    console.log(sip.start((data) => { console.log(data)} ))

    t1 = sip.transport.create({address: "127.0.0.1", type: 'tcp'})
    t2 = sip.transport.create({address: "127.0.0.1", type: 'tcp'})

    console.log("t1", t1)
    console.log("t2", t2)

    oc = sip.call.create(t1.id, {
        from_uri: '"abc"<sip:alice@test.com>',
        to_uri: `sip:bob@${t2.address}:${t2.port}`,
        headers: {
            'X-MyHeader1': 'abc',
            'X-MyHeader2': 'def',
        },
    })

    await z.wait([
        {
            event: "incoming_call",
            call_id: m.collect("call_id"),
            msg: sip_msg({
                $rm: 'INVITE',
                $fU: 'alice',
                $fd: 'test.com',
                $tU: 'bob',
                '$hdr(X-MyHeader1)': 'abc',
                'hdr_x_myheader2': 'def',
            }),
        },
        {
            event: 'response',
            call_id: oc.id,
            method: 'INVITE',
            msg: sip_msg({
                $rs: '100',
                $rr: 'Trying',
                '$(hdrcnt(via))': 1,
                'hdr_call_id': m.collect('sip_call_id'),
                $fU: 'alice',
                $fd: 'test.com',
                $tU: 'bob',
                '$hdr(l)': '0',
            }),
        },
    ], 1000)

    ic = {
        id: z.store.call_id,
        sip_call_id: z.store.sip_call_id,
    }

    sip.call.respond(ic.id, {
        code: 200,
        reason:'OK',
        headers: {
            'X-MyHeader3': 'ghi',
            'X-MyHeader4': 'jkl',
        },
    })

    await z.wait([
        {
            event: 'media_update',
            call_id: oc.id,
            status: 'ok',
        },
        {
            event: 'media_update',
            call_id: ic.id,
            status: 'ok',
        },
        {
            event: 'response',
            call_id: oc.id,
            method: 'INVITE',
            msg: sip_msg({
                $rs: '200',
                $rr: 'OK',
                '$(hdrcnt(v))': 1,
                $fU: 'alice',
                $fd: 'test.com',
                $tU: 'bob',
                '$hdr(content-type)': 'application/sdp',
                $rb: '!{_}a=sendrecv',
                '$hdr(X-MyHeader3)': 'ghi',
                '$hdr(X-MyHeader4)': 'jkl',
            }),
        },
    ], 1000)

    await z.sleep(200)

    sip.call.start_record_wav(oc.id, {file: './oc.wav'})
    sip.call.start_record_wav(ic.id, {file: './ic.wav'})

    sip.call.start_speech_synth(oc.id, {server_url: 'ws://0.0.0.0:8080', engine: 'gss', voice: 'en-US-Standard-E', language: 'en-US', text: 'hello world', times: 1})
    sip.call.start_speech_synth(ic.id, {server_url: 'ws://0.0.0.0:8080', engine: 'gss', voice: 'en-US-Standard-F', language: 'en-US', text: '<speak>Good morning<break time="2s"/>Good Afternoon</speak>', times: 1})

    sip.call.start_speech_recog(oc.id, {server_url: 'ws://0.0.0.0:8080', engine: 'gsr', language: 'en-US'})
    sip.call.start_speech_recog(ic.id, {server_url: 'ws://0.0.0.0:8080', engine: 'gsr', language: 'en-US'})

    await z.wait([
        {
            event: 'speech_synth_complete',
            call_id: ic.id,
        },
        {
            event: 'speech_synth_complete',
            call_id: oc.id,
        },
        {
            event: 'speech',
            call_id: oc.id,
            transcript: 'good morning',
        },
        {
            event: 'speech',
            call_id: ic.id,
            transcript: 'hello world',
        },
    ], 4000)

    await z.wait([
        {
            event: 'speech',
            call_id: oc.id,
            transcript: ' good afternoon',
        },
    ], 4000)

    await z.sleep(1000)

    sip.call.stop_record_wav(oc.id)
    sip.call.stop_record_wav(ic.id)

    sip.call.terminate(oc.id)

    await z.wait([
        {
            event: 'call_ended',
            call_id: oc.id,
        },
        {
            event: 'call_ended',
            call_id: ic.id,
        },
        {
            event: 'response',
            call_id: oc.id,
            method: 'BYE',
            msg: sip_msg({
                $rs: '200',
                $rr: 'OK',
            }),
        },
    ], 1000)

    await z.sleep(100)

    console.log("Success")

    sip.stop()
}


test()
.catch(e => {
    console.error(e)
    process.exit(1)
})

