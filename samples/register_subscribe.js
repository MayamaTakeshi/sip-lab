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

    var server = `${t2.ip}:${t2.port}`
    var domain = 'test1.com'

    var a1 = sip.account.create(t1.id, domain, server, 'user1', 'pass1')

    sip.account.register(a1, true)

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

    const s1 = sip.subscription_create(t1.id, 'dialog', 'application/dialog-info+xml', '<sip:user1@test1.com>', '<sip:user1@test1.com>', 'sip:park1@test1.com', `sip:${server}`, 'test1.com', 'user1', 'user1')

    sip.subscription_subscribe(s1, 120)

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

