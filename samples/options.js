var sip = require ('../index.js')
var Zeq = require('@mayama/zeq')
var z = new Zeq()
var m = require('data-matching')
var sip_msg = require('sip-matching')
var sdp = require('sdp-matching')
var assert = require('assert')

async function test() {
    sip.set_log_level(9)
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

    var server = `${t2.address}:${t2.port}`
    var domain = 'test1.com'

    var uac_req = sip.request.create(t1.id, {
        method: 'OPTIONS',
        from_uri: `sip:alice@${domain}`,
        to_uri: `sip:bob@${domain}`,
        request_uri: `sip:bob@${t2.address}:${t2.port}`,
        headers: {
            'X-MyHeader1': 'aaa',
            'X-MyHeader2': 'bbb',
        },
    })

    await z.wait([
        {
            event: 'non_dialog_request',
            request_id: m.collect('uas_req_id'),
            msg: sip_msg({
                $rm: 'OPTIONS',
                $fU: 'alice',
                $fd: domain,
                $tU: 'bob',
                $td: domain,
                '$hdr(X-MyHeader1)': 'aaa',
                'hdr_x_myheader2': 'bbb',
            }),
        },
    ], 1000)

    sip.request.respond(z.store.uas_req_id, {code: 200, reason: 'OK', headers: {'X-SomeHeader1': 'ccc', 'X-SomeHeader2': 'ddd'}})

    await z.wait([
        {
            event: 'response',
            request_id: uac_req.id,
            method: 'OPTIONS',
            msg: sip_msg({
                $rs: '200',
                $rr: 'OK',
                hdr_x_someheader1: 'ccc',
                hdr_x_someheader2: 'ddd',
            }),
        },
    ], 1000)

    console.log("Success")

    sip.stop()
    process.exit(0)
}


test()
.catch(e => {
    console.error(e)
    process.exit(1)
})

