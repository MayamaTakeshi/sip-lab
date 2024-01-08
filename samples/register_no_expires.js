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

    t1 = sip.transport.create({address: "127.0.0.1", port: 5090, type: 'udp'})
    t2 = sip.transport.create({address: "127.0.0.1", port: 5092, type: 'udp'})

    console.log("t1", t1)
    console.log("t2", t2)

    var server = `${t2.address}:${t2.port}`
    var domain = 'test1.com'

    var a1 = sip.account.create(t1.id, {
        domain, 
        server,
        username: 'user1',
        password: 'pass1',
        headers: {
            'X-MyHeader1': 'aaa',
            'X-MyHeader2': 'bbb',
        },
        expires: 0, // this will cause suppression of header Expires.
    })

    sip.account.register(a1, {auto_refresh: true})

    await z.wait([
        {
            event: 'non_dialog_request',
            request_id: m.collect('req_id'),
            msg: sip_msg({
                $rm: 'REGISTER',
                $fU: 'user1',
                $fd: domain,
                $tU: 'user1',
                $td: domain,
                '$hdr(X-MyHeader1)': 'aaa',
                hdr_x_myheader2: 'bbb',
                hdr_expires: m.absent,
            }),
        },
    ], 1000)

    sip.request.respond(z.store.req_id, {code: 200, reason: 'OK', headers: {Expires: '120'}})

    await z.wait([
        {
            event: 'registration_status',
            account_id: a1.id,
            code: 200,
            reason: 'OK',
            expires: 120,
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

