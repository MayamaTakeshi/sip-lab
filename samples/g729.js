var sip = require ('../index.js')
var Zeq = require('@mayama/zeq')
var z = new Zeq()
var m = require('data-matching')
var sip_msg = require('sip-matching')
var sdp = require('sdp-matching')

var assert = require('assert')

async function test() {
    //sip.set_log_level(6)
    sip.dtmf_aggregation_on(500)

    z.trap_events(sip.event_source, 'event', (evt) => {
        var e = evt.args[0]
        return e
    })

    console.log(sip.start((data) => { console.log(data)} ))

    t1 = sip.transport.create({address: "127.0.0.1", type: 'udp'})
    t2 = sip.transport.create({address: "127.0.0.1", type: 'udp'})

    console.log("t1", t1)
    console.log("t2", t2)

    sip.set_codecs("g729/8000/1:128")

    flags = 0

    oc = sip.call.create(t1.id, {from_uri: 'sip:alice@test.com', to_uri: `sip:bob@${t2.address}:${t2.port}`})

    await z.wait([
        {
            event: "incoming_call",
            call_id: m.collect("call_id"),
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
                'hdr_l': '0',
            }),
        },
    ], 1000)

    ic = {
        id: z.store.call_id,
        sip_call_id: z.store.sip_call_id,
    }

    sip.call.respond(ic.id, {code: 200, reason: 'OK'})

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
                '$(hdrcnt(VIA))': 1,
                $fU: 'alice',
                $fd: 'test.com',
                $tU: 'bob',
                'hdr_content_type': 'application/sdp',
                $rb: '!{_}a=sendrecv',
            }),
        },
    ], 1000)

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

    sip.call.reinvite(oc.id, false, 0)

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

    oc_stat = sip.call.get_stream_stat(oc.id, {media_id: 0})
    ic_stat = sip.call.get_stream_stat(ic.id, {media_id: 0})

    console.log(oc_stat)
    console.log(ic_stat)

    oc_stat = JSON.parse(oc_stat)
    ic_stat = JSON.parse(ic_stat)

    assert(oc_stat.CodecInfo == 'G729/8000/1')
    assert(ic_stat.CodecInfo == 'G729/8000/1')

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

    console.log("Success")

    sip.stop()
}

test()
.catch(e => {
    console.error(e)
    process.exit(1)
})
