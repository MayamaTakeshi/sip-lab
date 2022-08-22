var sip = require ('../index.js')
var Zester = require('zester')
var z = new Zester()
var m = require('data-matching')
var sip_msg = require('sip-matching')

async function test() {
    //sip.set_log_level(6)
    sip.dtmf_aggregation_on(500)

    z.trap_events(sip.event_source, 'event', (evt) => {
        var e = evt.args[0]
        return e
    })

    console.log(sip.start((data) => { console.log(data)} ))

    t1 = sip.transport.create("127.0.0.1", 5090, 1)
    t2 = sip.transport.create("127.0.0.1", 5092, 1)

    console.log("t1", t1)
    console.log("t2", t2)

    flags = 0

    oc = sip.call.create(t1.id, flags, 'sip:a@t', 'sip:b@127.0.0.1:5092')

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
                '$hdr(call-id)': m.collect('sip_call_id'),
                $fU: 'a',
                $fd: 't',
                $tU: 'b',
                '$hdr(l)': '0',
            }),
        },
    ], 1000)

    ic = {
        id: z.store.call_id,
        sip_call_id: z.store.sip_call_id,
    }

    sip.call.respond(ic.id, 200, 'OK')

    await z.wait([
        {
            event: 'media_status',
            call_id: oc.id,
            status: 'setup_ok',
            local_mode: 'sendrecv',
            remote_mode: 'sendrecv',
        },
        {
            event: 'media_status',
            call_id: ic.id,
            status: 'setup_ok',
            local_mode: 'sendrecv',
            remote_mode: 'sendrecv',
        },
        {
            event: 'response',
            call_id: oc.id,
            method: 'INVITE',
            msg: sip_msg({
                $rs: '200',
                $rr: 'OK',
                '$(hdrcnt(VIA))': 1,
                $fU: 'a',
                $fd: 't',
                $tU: 'b',
                '$hdr(content-type)': 'application/sdp',
                $rb: '!{_}a=sendrecv',
            }),
        },
    ], 1000)

    await z.sleep(1000)

    var is_sender = true

    var in_file = 'samples/artifacts/this-is-never-ok.tiff'
    var out_file = "received.tiff"

    sip.call.start_fax(oc.id, is_sender, in_file)
    sip.call.start_fax(ic.id, !is_sender, out_file)

    await z.wait([
        {
            event: 'fax_result',
            call_id: oc.id,
            result: 0,
        },
        {
            event: 'fax_result',
            call_id: ic.id,
            result: 0,
        },
    ], 180 * 1000)

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

    console.log(`Success. Fax was transmitted as ${in_file} and received as ${out_file}`)

    sip.stop()
}


test()
.catch(e => {
    console.error(e)
    process.exit(1)
})

