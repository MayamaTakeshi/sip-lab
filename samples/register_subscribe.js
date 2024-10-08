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

    var a1 = sip.account.create(t1.id, {
        domain, 
        server,
        username: 'user1',
        password: 'pass1',
        headers: {
            'X-MyHeader1': 'aaa',
            'X-MyHeader2': 'bbb',
        },
        expires: 60,
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
                hdr_expires: '60',
            }),
        },
    ], 1000)

    sip.request.respond(z.store.req_id, {code: 200, reason: 'OK', headers: {Expires: '60'}})

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

    const sub_expires = 120 

    sip.subscription.subscribe(s1, {expires: sub_expires})

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
                'hdr_accept': 'application/dialog-info+xml',
                'hdr_allow_events': 'refer, dialog',
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
                'hdr_event': 'dialog',
                '$hdr(Subscription-State)': 'active;expires=!{sub_expires:num}',
                '$hdr(Allow-Events)': 'refer, dialog',
            }),
        },
    ], 1000)

    // Subscription-State expires will be computed by pjsip. It might not be the exact value of sub_expires due to latency so we give 2 seconds of tolerance
    assert(z.store.sub_expires > (sub_expires - 2))

    sip.subscriber.notify(subscriber_id, {
        content_type: 'application/dialog-info+xml',
        body: '<dialog>bla bla bla</dialog>', 
        subscription_state: 4,
        reason: 'normal',
    })

    await z.wait([
        {
            event: 'request',
            subscription_id: s1,
            msg: sip_msg({
                $rm: 'NOTIFY',
                hdr_event: 'dialog',
                hdr_subscription_state: 'active;expires=!{expires}',
                hdr_content_type: 'application/dialog-info+xml',
                $rb: '<dialog>bla bla bla</dialog>',
            }),
        },
    ], 1000)
    
    await z.sleep(100)

    z.store.req_id = null

    sip.account.unregister(a1)

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
                hdr_expires: '0',
            }),
        },
    ], 1000)

    sip.request.respond(z.store.req_id, {code: 200, reason: 'OK', headers: {Expires: '0'}})

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
    process.exit(0)
}


test()
.catch(e => {
    console.error(e)
    process.exit(1)
})

