const addon = require('node-gyp-build')(__dirname);

var events = require('events');
var eventEmitter = new events.EventEmitter();


process.on('SIGINT', function() {
	console.log("SIGINT");
	var res = addon.shutdown(1)
	process.exit(1)
});

addon.event_source = eventEmitter

var timerId = setInterval(() => {
	res = addon.do_poll();
	if(res) {
		//console.log("poll res", res)
		var evt
		var pos = res.indexOf("\n")
		if(pos > 0) {
			var json = res.substring(0, pos)
			evt = JSON.parse(json)
			var msg = res.substring(pos+1)
			if(msg != "") {
				evt.msg = msg
			}
		} else {
			evt = JSON.parse(res)
		}
		eventEmitter.emit("event", evt)
	}
}, 50)

addon.stop = (clean_up = false) => {
	clearInterval(timerId)
	var res = addon.shutdown(clean_up ? 1 : 0)
}

addon.transport = {
  create: (params) => { return addon.transport_create(JSON.stringify(params)) },
}

addon.account = {
  create: (t_id, params) => { return addon.account_create(t_id, JSON.stringify(params)) },
  register: (a_id, params) => { return addon.account_register(a_id, JSON.stringify(params ? params : {})) },
  unregister: addon.account_unregister,
}

addon.request = {
  create: (t_id, params) => { return addon.request_create(t_id, JSON.stringify(params)) },
  respond: (r_id, params) => { return addon.request_respond(r_id, JSON.stringify(params)) },
}

addon.call = {
  create: (t_id, params) => { return addon.call_create(t_id, JSON.stringify(params)) },
  respond: (c_id, params) => { return addon.call_respond(c_id, JSON.stringify(params)) },
  terminate: (c_id, params) => { return addon.call_terminate(c_id, JSON.stringify(params ? params : {})) },
  send_dtmf: (c_id, params) => { return addon.call_send_dtmf(c_id, JSON.stringify(params)) },
  send_bfsk: (c_id, params) => { return addon.call_send_bfsk(c_id, JSON.stringify(params)) },
  reinvite: (c_id, params) => { return addon.call_reinvite(c_id, JSON.stringify(params ? params : {})) },
  update: (c_id, params) => { return addon.call_update(c_id, JSON.stringify(params ? params : {})) },
  send_request: (c_id, params) => { return addon.call_send_request(c_id, JSON.stringify(params)) },
  start_record_wav: (c_id, params) => { return addon.call_start_record_wav(c_id, JSON.stringify(params)) },
  start_play_wav: (c_id, params) => { return addon.call_start_play_wav(c_id, JSON.stringify(params)) },
  stop_record_wav: (c_id, params) => { return addon.call_stop_record_wav(c_id, JSON.stringify(params ? params : {})) },
  stop_play_wav: (c_id, params) => { return addon.call_stop_play_wav(c_id, JSON.stringify(params ? params : {})) },
  start_fax: (c_id, params) => { return addon.call_start_fax(c_id, JSON.stringify(params)) },
  stop_fax: (c_id, params) => { return addon.call_stop_fax(c_id, JSON.stringify(params ? params : {})) },
  start_speech_synth: (c_id, params) => { return addon.call_start_speech_synth(c_id, JSON.stringify(params)) },
  stop_speech_synth: (c_id, params) => { return addon.call_stop_speech_synth(c_id, JSON.stringify(params ? params : {})) },

  start_inband_dtmf_detection: (c_id, params) => { return addon.call_start_inband_dtmf_detection(c_id, JSON.stringify(params ? params : {})) },
  stop_inband_dtmf_detection: (c_id, params) => { return addon.call_stop_inband_dtmf_detection(c_id, JSON.stringify(params ? params : {})) },

  start_bfsk_detection: (c_id, params) => { return addon.call_start_bfsk_detection(c_id, JSON.stringify(params ? params : {})) },
  stop_bfsk_dtmf_detection: (c_id, params) => { return addon.call_stop_bfsk_detection(c_id, JSON.stringify(params ? params : {})) },

  start_speech_recog: (c_id, params) => { 
    var ps = {}
    if(params) {
      ps = {...params}
    }

    if(ps.model_path) {
      process.env.POCKETSPHINX_PATH = ps.model_path
      delete ps.model_path
    } else {
      process.env.POCKETSPHINX_PATH = __dirname + "/pocketsphinx/model"
    }
    return addon.call_start_speech_recog(c_id, JSON.stringify(ps))
  },
   
  stop_speech_recog: (c_id, params) => { return addon.call_stop_speech_recog(c_id, JSON.stringify(params ? params : {})) },

  get_stream_stat: (c_id, params) => { return addon.call_get_stream_stat(c_id, JSON.stringify(params ? params : {})) },
  //refer: (c_id, params) => { return addon.call_refer(c_id, JSON.stringify(params)) },
  get_info: addon.call_get_info,
  gen_string_replaces: addon.call_gen_string_replaces,

  send_mrcp_msg: (c_id, params) => { return addon.call_send_tcp_msg(c_id, JSON.stringify(params ? params : {})) },
}

addon.subscriber = {
    notify: (s_id, params) => { return addon.notify(s_id, JSON.stringify(params)) },
    notify_xfer: (s_id, params) => { return addon.notify_xfer(s_id, JSON.stringify(params)) },
}

addon.subscription = {
    create: (t_id, params) => { return addon.subscription_create(t_id, JSON.stringify(params)) },
    subscribe: (s_id, params) => { return addon.subscription_subscribe(s_id, JSON.stringify(params)) },
}

module.exports = addon;
