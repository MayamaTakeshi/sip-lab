'use strict'

const net = require('net')
const path = require('path')
const fs = require('fs')
const { spawn } = require('child_process')
const { EventEmitter } = require('events')

/* ------------------------------------------------------------------ */
/* Locate the server binary.                                           */
/* ------------------------------------------------------------------ */
function findBinary() {
  const candidates = [
    path.join(__dirname, 'sip_lab_server'),
    path.join(__dirname, 'build', 'Release', 'sip_lab_server'),
    path.join(__dirname, 'build', 'Debug', 'sip_lab_server'),
  ]
  for (const p of candidates) {
    try { fs.accessSync(p, fs.constants.X_OK); return p } catch (_) {}
  }
  throw new Error(
    'sip-lab: could not find sip_lab_server binary. Run "make" or "npm run build-server" first.'
  )
}

/* ------------------------------------------------------------------ */
/* Internal state                                                      */
/* ------------------------------------------------------------------ */
const eventEmitter = new EventEmitter()
let _socket = null
let _seq = 0
const _pending = new Map()   // seq -> { resolve, reject }
let _recvBuf = ''            // partial line accumulator

let _serverProcess = null
let _connected = false
let _connectPromise = null  // guards against concurrent _connect calls

/* ------------------------------------------------------------------ */
/* Parse lines arriving from the server.                               */
/* ------------------------------------------------------------------ */
function _handleLine(line) {
  line = line.trim()
  if (!line) return

  let msg
  try { msg = JSON.parse(line) } catch (_) {
    eventEmitter.emit('_raw_msg', line)
    return
  }

  if (typeof msg.seq === 'number') {
    const pending = _pending.get(msg.seq)
    if (pending) {
      _pending.delete(msg.seq)
      if (msg.error !== undefined) pending.reject(new Error(msg.error))
      else pending.resolve(msg.result !== undefined ? msg.result : null)
    }
    return
  }

  eventEmitter.emit('event', msg)
}

/* ------------------------------------------------------------------ */
/* Socket data handler                                                 */
/* ------------------------------------------------------------------ */
function _onSocketData(chunk) {
  _recvBuf += chunk.toString('utf8')
  let nl
  while ((nl = _recvBuf.indexOf('\n')) !== -1) {
    const line = _recvBuf.slice(0, nl)
    _recvBuf = _recvBuf.slice(nl + 1)
    _handleLine(line)
  }
}

function _rejectAll(err) {
  for (const [, p] of _pending) p.reject(err)
  _pending.clear()
}

/* ------------------------------------------------------------------ */
/* Start the server process and connect.  Called lazily on the first   */
/* command.  Returns a Promise that resolves once the TCP link is up. */
/* Concurrent calls are coalesced into a single connection attempt.    */
/* ------------------------------------------------------------------ */
function _connect() {
  if (_connected) return Promise.resolve()
  if (_connectPromise) return _connectPromise

  _connectPromise = new Promise((resolve, reject) => {
    const binPath = findBinary()
    const serverEnv = Object.assign({}, process.env, {
      POCKETSPHINX_PATH: path.join(__dirname, 'pocketsphinx', 'model'),
    })
    const child = spawn(binPath, [], { stdio: ['ignore', 'inherit', 'pipe'], env: serverEnv })
    _serverProcess = child

    let settled = false
    const settle = (fn, val) => { if (!settled) { settled = true; fn(val) } }

    child.on('error', (err) => settle(reject, err))
    child.on('exit', (code, signal) => {
      settle(reject, new Error(
        `sip-lab-server exited before READY (code=${code} signal=${signal})`
      ))
    })

    child.stderr.on('data', function onStderr(chunk) {
      const text = chunk.toString('utf8')
      // Scan for READY <port> across the accumulated stream
      const m = text.match(/READY\s+(\d+)/)
      if (!m) return
      child.stderr.removeListener('data', onStderr)
      // Forward any subsequent stderr from the server
      child.stderr.on('data', (chunk) => process.stderr.write(chunk))

      const port = parseInt(m[1], 10)
      const sock = net.connect(port, '127.0.0.1')
      _socket = sock

      sock.on('connect', () => {
        _connected = true
        sock.on('data', _onSocketData)
        settle(resolve)
      })

      sock.on('error', (err) => settle(reject, err))
      sock.on('close', () => _rejectAll(new Error('sip-lab-server connection closed')))
    })
  })

  return _connectPromise
}

/* ------------------------------------------------------------------ */
/* Send a command and return a Promise for the response.               */
/* ------------------------------------------------------------------ */
async function _cmd(obj) {
  await _connect()

  return new Promise((resolve, reject) => {
    const seq = ++_seq
    obj.seq = seq
    _pending.set(seq, { resolve, reject })
    _socket.write(JSON.stringify(obj) + '\n')
  })
}

/* ------------------------------------------------------------------ */
/* Public API                                                          */
/* ------------------------------------------------------------------ */
const addon = { event_source: eventEmitter }

process.on('SIGINT', () => {
  console.log('SIGINT')
  addon.stop(false)
  process.exit(1)
})

addon.stop = async (cleanUp = false) => {
  if (_connected) {
    try { await _cmd({ cmd: 'shutdown', clean_up: cleanUp ? 1 : 0 }) } catch (_) {}
  }
  if (_serverProcess) {
    setTimeout(() => { if (_serverProcess) _serverProcess.kill() }, 500)
  }
}

// Compatibility stub — old addon.start() was a NAPI initialisation hook.
// In the socket model the server starts lazily on first command.
addon.start = (_cb) => undefined

addon.transport = {
  create: (params) =>
    _cmd({ cmd: 'transport_create', params: JSON.stringify(params) }),
}

addon.account = {
  create: (t_id, params) =>
    _cmd({ cmd: 'account_create', transport_id: t_id, params: JSON.stringify(params) }),
  register: (a_id, params) =>
    _cmd({ cmd: 'account_register', account_id: a_id, params: JSON.stringify(params || {}) }),
  unregister: (a_id) =>
    _cmd({ cmd: 'account_unregister', account_id: a_id }),
}

addon.request = {
  create: (t_id, params) =>
    _cmd({ cmd: 'request_create', transport_id: t_id, params: JSON.stringify(params) }),
  respond: (r_id, params) =>
    _cmd({ cmd: 'request_respond', request_id: r_id, params: JSON.stringify(params) }),
}

addon.call = {
  create: (t_id, params) =>
    _cmd({ cmd: 'call_create', transport_id: t_id, params: JSON.stringify(params) }),
  respond: (c_id, params) =>
    _cmd({ cmd: 'call_respond', call_id: c_id, params: JSON.stringify(params) }),
  terminate: (c_id, params) =>
    _cmd({ cmd: 'call_terminate', call_id: c_id, params: JSON.stringify(params || {}) }),
  send_dtmf: (c_id, params) =>
    _cmd({ cmd: 'call_send_dtmf', call_id: c_id, params: JSON.stringify(params) }),
  send_bfsk: (c_id, params) =>
    _cmd({ cmd: 'call_send_bfsk', call_id: c_id, params: JSON.stringify(params) }),
  reinvite: (c_id, params) =>
    _cmd({ cmd: 'call_reinvite', call_id: c_id, params: JSON.stringify(params || {}) }),
  update: (c_id, params) =>
    _cmd({ cmd: 'call_update', call_id: c_id, params: JSON.stringify(params || {}) }),
  send_request: (c_id, params) =>
    _cmd({ cmd: 'call_send_request', call_id: c_id, params: JSON.stringify(params) }),
  start_record_wav: (c_id, params) =>
    _cmd({ cmd: 'call_start_record_wav', call_id: c_id, params: JSON.stringify(params) }),
  start_play_wav: (c_id, params) =>
    _cmd({ cmd: 'call_start_play_wav', call_id: c_id, params: JSON.stringify(params) }),
  stop_record_wav: (c_id, params) =>
    _cmd({ cmd: 'call_stop_record_wav', call_id: c_id, params: JSON.stringify(params || {}) }),
  stop_play_wav: (c_id, params) =>
    _cmd({ cmd: 'call_stop_play_wav', call_id: c_id, params: JSON.stringify(params || {}) }),
  start_fax: (c_id, params) =>
    _cmd({ cmd: 'call_start_fax', call_id: c_id, params: JSON.stringify(params) }),
  stop_fax: (c_id, params) =>
    _cmd({ cmd: 'call_stop_fax', call_id: c_id, params: JSON.stringify(params || {}) }),
  start_speech_synth: (c_id, params) =>
    _cmd({ cmd: 'call_start_speech_synth', call_id: c_id, params: JSON.stringify(params) }),
  stop_speech_synth: (c_id, params) =>
    _cmd({ cmd: 'call_stop_speech_synth', call_id: c_id, params: JSON.stringify(params || {}) }),
  start_inband_dtmf_detection: (c_id, params) =>
    _cmd({ cmd: 'call_start_inband_dtmf_detection', call_id: c_id, params: JSON.stringify(params || {}) }),
  stop_inband_dtmf_detection: (c_id, params) =>
    _cmd({ cmd: 'call_stop_inband_dtmf_detection', call_id: c_id, params: JSON.stringify(params || {}) }),
  start_bfsk_detection: (c_id, params) =>
    _cmd({ cmd: 'call_start_bfsk_detection', call_id: c_id, params: JSON.stringify(params || {}) }),
  stop_bfsk_detection: (c_id, params) =>
    _cmd({ cmd: 'call_stop_bfsk_detection', call_id: c_id, params: JSON.stringify(params || {}) }),
  start_speech_recog: (c_id, params) => {
    const ps = params ? { ...params } : {}
    delete ps.model_path  // handled via server env POCKETSPHINX_PATH
    return _cmd({ cmd: 'call_start_speech_recog', call_id: c_id, params: JSON.stringify(ps) })
  },
  stop_speech_recog: (c_id, params) =>
    _cmd({ cmd: 'call_stop_speech_recog', call_id: c_id, params: JSON.stringify(params || {}) }),
  get_stream_stat: (c_id, params) =>
    _cmd({ cmd: 'call_get_stream_stat', call_id: c_id, params: JSON.stringify(params || {}) }),
  get_info: (c_id, required_info) =>
    _cmd({ cmd: 'call_get_info', call_id: c_id, required_info }),
  gen_string_replaces: (c_id) =>
    _cmd({ cmd: 'call_gen_string_replaces', call_id: c_id }),
  send_mrcp_msg: (c_id, params) =>
    _cmd({ cmd: 'call_send_tcp_msg', call_id: c_id, params: JSON.stringify(params || {}) }),
}

addon.subscriber = {
  notify: (s_id, params) =>
    _cmd({ cmd: 'notify', subscriber_id: s_id, params: JSON.stringify(params) }),
  notify_xfer: (s_id, params) =>
    _cmd({ cmd: 'notify_xfer', subscriber_id: s_id, params: JSON.stringify(params) }),
}

addon.subscription = {
  create: (t_id, params) =>
    _cmd({ cmd: 'subscription_create', transport_id: t_id, params: JSON.stringify(params) }),
  subscribe: (s_id, params) =>
    _cmd({ cmd: 'subscription_subscribe', subscription_id: s_id, params: JSON.stringify(params) }),
}

addon.set_opus_config = (params) =>
  _cmd({ cmd: 'set_opus_config', params: JSON.stringify(params) })

addon.dtmf_aggregation_on = (inter_digit_timer) =>
  _cmd({ cmd: 'dtmf_aggregation_on', inter_digit_timer })

addon.dtmf_aggregation_off = () =>
  _cmd({ cmd: 'dtmf_aggregation_off' })

addon.get_codecs = () =>
  _cmd({ cmd: 'get_codecs' })

addon.set_codecs = (codec_info) =>
  _cmd({ cmd: 'set_codecs', codec_info })

addon.set_log_level = (log_level) =>
  _cmd({ cmd: 'set_log_level', log_level })

addon.set_flags = (flags) =>
  _cmd({ cmd: 'set_flags', flags })

addon.enable_telephone_event = () =>
  _cmd({ cmd: 'enable_telephone_event' })

addon.disable_telephone_event = () =>
  _cmd({ cmd: 'disable_telephone_event' })

addon.register_pkg = (event, accept) =>
  _cmd({ cmd: 'register_pkg', event, accept })

module.exports = addon
