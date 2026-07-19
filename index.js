'use strict'

const net = require('net')
const path = require('path')
const fs = require('fs')
const { spawn } = require('child_process')
const { EventEmitter } = require('events')
const deasync = require('deasync')

/* ------------------------------------------------------------------ */
/* Locate the server binary (built by node-gyp).                      */
/* ------------------------------------------------------------------ */
function findBinary() {
  const candidates = [
    path.join(__dirname, 'build', 'Release', 'sip_lab_server'),
    path.join(__dirname, 'build', 'Debug', 'sip_lab_server'),
  ]
  for (const p of candidates) {
    try { fs.accessSync(p, fs.constants.X_OK); return p } catch (_) {}
  }
  throw new Error(
    'sip-lab: could not find sip_lab_server binary. Run "npm run build" first.'
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

/* ------------------------------------------------------------------ */
/* Parse lines arriving from the server.                              */
/* ------------------------------------------------------------------ */
function _handleLine(line) {
  line = line.trim()
  if (!line) return

  let msg
  try { msg = JSON.parse(line) } catch (_) {
    // Non-JSON line — forwarded as raw event (e.g. MRCP message body)
    eventEmitter.emit('_raw_msg', line)
    return
  }

  if (typeof msg.seq === 'number') {
    // Response to a command
    const pending = _pending.get(msg.seq)
    if (pending) {
      _pending.delete(msg.seq)
      if (msg.error !== undefined) pending.reject(new Error(msg.error))
      else pending.resolve(msg.result !== undefined ? msg.result : null)
    }
    return
  }

  // Async event from the SIP engine — emit to callers
  eventEmitter.emit('event', msg)
}

/* ------------------------------------------------------------------ */
/* Start the server process and connect synchronously.                 */
/* Called lazily on the first command.                                 */
/* ------------------------------------------------------------------ */
function _connect() {
  if (_connected) return

  const binPath = findBinary()
  const child = spawn(binPath, [], { stdio: ['ignore', 'inherit', 'pipe'] })
  _serverProcess = child

  // Read "READY <port>" from the child's stdout synchronously
  let portLine = ''
  let portReady = false
  let startupError = null

  child.on('error', (err) => { startupError = err })
  child.on('exit', (code, signal) => {
    if (!_connected) {
      startupError = new Error(
        `sip-lab-server exited before READY (code=${code} signal=${signal})`
      )
      portReady = true  // unblock the wait loop
    }
  })

  // Buffer stderr until we have the READY line; after that stderr is
  // no longer needed (all pjsip log output goes via inherited stdout).
  child.stderr.on('data', (chunk) => {
    portLine += chunk.toString('utf8')
    const nl = portLine.indexOf('\n')
    if (nl !== -1 && !portReady) {
      // Check each line for READY
      const lines = portLine.split('\n')
      for (let i = 0; i < lines.length; i++) {
        const m = lines[i].match(/^READY\s+(\d+)/)
        if (m) {
          const port = parseInt(m[1], 10)
          portLine = ''

          const sock = net.connect(port, '127.0.0.1')
          _socket = sock

          sock.on('connect', () => {
            _connected = true
            portReady = true
            sock.on('data', _onSocketData)
          })

          sock.on('error', (err) => {
            if (!_connected) { startupError = err; portReady = true }
            else _rejectAll(err)
          })

          sock.on('close', () => _rejectAll(new Error('sip-lab-server connection closed')))

          return
        }
      }
    }
  })

  // Synchronously wait for the READY line and TCP connection
  deasync.loopWhile(() => !portReady)
  if (startupError) throw startupError
}

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
/* Send a command and block synchronously until the response arrives.  */
/* ------------------------------------------------------------------ */
function _cmd(obj) {
  _connect()

  let done = false, result, error

  const seq = ++_seq
  obj.seq = seq
  _pending.set(seq, {
    resolve: (v) => { result = v; done = true },
    reject:  (e) => { error = e;  done = true },
  })

  _socket.write(JSON.stringify(obj) + '\n')

  deasync.loopWhile(() => !done)

  if (error) throw error
  return result
}

/* ------------------------------------------------------------------ */
/* Public API object — identical surface to the old NAPI addon.        */
/* ------------------------------------------------------------------ */
const addon = { event_source: eventEmitter }

process.on('SIGINT', () => {
  console.log('SIGINT')
  addon.stop(false)
  process.exit(1)
})

addon.stop = (cleanUp = false) => {
  try { _cmd({ cmd: 'shutdown', clean_up: cleanUp ? 1 : 0 }) } catch (_) {}
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
    if (ps.model_path) {
      process.env.POCKETSPHINX_PATH = ps.model_path
      delete ps.model_path
    } else {
      process.env.POCKETSPHINX_PATH = path.join(__dirname, 'pocketsphinx', 'model')
    }
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
