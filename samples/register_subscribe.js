var sip = require ('../index.js')
var Zeq = require('@mayama/zeq')
var z = new Zeq()
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
    })

    sip.account.register(a1, {auto_refresh: true})

    await z.wait([
        {
            event: 'non_dialog_request',
            msg: sip_msg({
                $rm: 'REGISTER',
                $fU: 'user1',
                $fd: domain,
                $tU: 'user1',
                $td: domain,
                '$hdr(X-MyHeader1)': 'aaa',
                '$hdr(X-MyHeader2)': 'bbb',
            }),
        },
    ], 1000)

    // sip-lab automatically replies with '200 OK' to non_dialog_request.

    await z.wait([
        {
            event: 'registration_status',
            account_id: a1.id,
            code: 200,
            reason: 'OK',
            expires: 60
        },
    ], 1000)

    const s1 = sip.subscription.create(t1.id, {
            event: 'dialog',
            accept: 'application/dialog-info+xml',
            from_uri: '<sip:user1@test1.com>',
            to_uri: '<sip:user1@test1.com>',
            request_uri: 'sip:park1@test1.com',
            proxy_uri: `sip:${server}`,
            auth: {
                realm: 'test1.com',
                username: 'user1',
                password: 'user1',
            },
    })

    sip.subscription.subscribe(s1, {expires: 120})

    await z.wait([
        {
            event: 'request',
            subscriber_id: m.collect('subscriber_id'),
            msg: sip_msg({
                $rm: 'SUBSCRIBE',
                $ru: 'sip:park1@test1.com',
                $fU: 'user1',
                $fd: domain,
                '$hdr(Event)': 'dialog',
                '$hdr(Accept)': 'application/dialog-info+xml',
                '$hdr(Allow-Events)': 'refer, dialog',
            })
        },
    ], 1000)

    var subscriber_id = z.store.subscriber_id

    await z.wait([
        {
            event: 'response',
            subscription_id: subscriber_id,
            method: 'SUBSCRIBE',
            msg: sip_msg({
                $rs: '200',
                $rr: 'OK',
            })
        },
    ], 1000)

    await z.wait([
        {
            subscription_id: s1,
            msg: sip_msg({
                $rm: 'NOTIFY',
                '$hdr(Event)': 'dialog',
                '$hdr(Subscription-State)': 'active;expires=120',
                '$hdr(Allow-Events)': 'refer, dialog',
            }),
        },
    ], 1000)

    sip.account.unregister(a1)

    await z.wait([
        {
            event: 'non_dialog_request',
            msg: sip_msg({
                $rm: 'REGISTER',
                $fU: 'user1',
                $fd: domain,
                $tU: 'user1',
                $td: domain,
            }),
        },
    ], 1000)

    await z.wait([
        {
            event: 'registration_status',
            account_id: a1.id,
            code: 200,
            reason: 'OK',
            expires: 0,
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

