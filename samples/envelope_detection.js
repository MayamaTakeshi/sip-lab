var sip = require('../index.js')
var Zeq = require('@mayama/zeq')
var z = new Zeq()
var m = require('data-matching')
var sip_msg = require('sip-matching')
var path = require('path')

var REF_WAV = path.join(__dirname, 'artifacts', 'hello_world.wav')

async function test() {
  sip.dtmf_aggregation_on(500)

  sip.set_codecs("g729/8000/1:128")

  z.trap_events(sip.event_source, 'event', (evt) => {
    var e = evt.args[0]
    return e
  })

  console.log(await sip.start((data) => { console.log(data) }))

  t1 = await sip.transport.create({address: "127.0.0.1", type: 'tcp'})
  t2 = await sip.transport.create({address: "127.0.0.1", type: 'tcp'})

  console.log("t1", t1)
  console.log("t2", t2)

  oc = await sip.call.create(t1.id, {
    from_uri: '"abc"<sip:alice@test.com>',
    to_uri: 'sip:bob@' + t2.address + ':' + t2.port,
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
      }),
    },
    {
      event: 'response',
      call_id: oc.id,
      method: 'INVITE',
      msg: sip_msg({
        $rs: '100',
        $rr: 'Trying',
        'hdr_call_id': m.collect('sip_call_id'),
      }),
    },
  ], 1000)

  ic = {
    id: z.$call_id,
    sip_call_id: z.$sip_call_id,
  }

  sip.call.respond(ic.id, {
    code: 200,
    reason: 'OK',
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
      }),
    },
  ], 1000)

  await z.sleep(100)

  sip.call.start_play_wav(oc.id, {file: REF_WAV, end_of_file_event: true})

  console.log("Starting envelope detection")
  sip.call.start_envelope_detection(ic.id, {
    ref_file: REF_WAV,
    threshold: 0.3,
    cooldown_ms: 500,
    check_stride: 2,
  })

  for(var i=0 ; i<3 ; i++) {
      await z.wait([
        {
          event: 'end_of_file',
          call_id: oc.id
        },
        {
          event: 'envelope_match',
          call_id: ic.id,
        },
      ], 6000)
      console.log('envelope_match detected')
  }

  sip.call.stop_envelope_detection(ic.id)
  sip.call.stop_play_wav(oc.id)

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

  await z.sleep(50)

  console.log('Success')

  await sip.stop()
  process.exit(0)
}

test()
.catch(async e => {
  console.error(e)
  process.exit(1)
})
