var sip = require ('../index.js')
var Zeq = require('@mayama/zeq')
var z = new Zeq()
var m = require('data-matching')
var sip_msg = require('sip-matching')
var sdp = require('sdp-matching')

async function test() {
    //sip.set_log_level(6)
    sip.dtmf_aggregation_on(500)

    z.trap_events(sip.event_source, 'event', (evt) => {
        var e = evt.args[0]
        return e
    })

    console.log(sip.start((data) => { console.log(data)} ))

    t1 = sip.transport.create({address: "127.0.0.1"})
    t2 = sip.transport.create({address: "127.0.0.1"})

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

    sip.call.start_record_wav(oc.id, {file: './oc.wav'})
    sip.call.start_record_wav(ic.id, {file: './ic.wav'})

    await z.sleep(100)

    sip.call.start_play_wav(oc.id, {file: 'samples/artifacts/yosemitesam.wav', end_of_file_event: true, no_loop: true})
    sip.call.start_play_wav(ic.id, {file: 'samples/artifacts/yosemitesam.wav', end_of_file_event: true, no_loop: true})

    sip.call.reinvite(oc.id)

    await z.wait([
        {
            event: 'reinvite',
            call_id: ic.id
        },
    ], 1000)

    sip.call.respond(ic.id, {code: 200, reason: 'OK'})

    await z.wait([
        {
            event: 'response',
            call_id: oc.id,
            method: 'INVITE',
            msg: sip_msg({
                $rs: '100',
            }),
        },
        {
            event: 'response',
            call_id: oc.id,
            method: 'INVITE',
            msg: sip_msg({
                $rs: '200',
                $rr: 'OK',
                $rb: '!{_}a=sendrecv',
            }),
        },
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
    ], 500)

    sip.call.reinvite(ic.id)

    await z.wait([
        {
            event: 'reinvite',
            call_id: oc.id
        },
    ], 1000)

    sip.call.respond(oc.id, {code: 200, reason: 'OK'})

    await z.wait([
        {
            event: 'response',
            call_id: ic.id,
            method: 'INVITE',
            msg: sip_msg({
                $rs: '100',
            }),
        },
        {
            event: 'response',
            call_id: ic.id,
            method: 'INVITE',
            msg: sip_msg({
                $rs: '200',
                $rr: 'OK',
                $rb: '!{_}a=sendrecv',
            }),
        },
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
    ], 500)

    await z.wait([
        {
            event: 'end_of_file',
            call_id: ic.id,
        },
        {
            event: 'end_of_file',
            call_id: oc.id,
        },
    ], 3000)

    await z.sleep(3000) // we should not receive end_of_file events again

    stat1 = sip.call.get_stream_stat(oc.id, {media_id: 0})
    stat2 = sip.call.get_stream_stat(ic.id, {media_id: 0})

    console.log("stat1", stat1)
    console.log("stat2", stat2)

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

    await z.sleep(1000)

    console.log("Success")

    sip.stop()
}


test()
.catch(e => {
    console.error(e)
    process.exit(1)
})

