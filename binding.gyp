{
  'targets': [
    {
      'target_name': 'all-settings',
      'type': 'none',
      'all_dependent_settings': {
          'include_dirs': [
            "3rdParty/pjproject/pjsip/include",
            "3rdParty/pjproject/pjlib/include",
            "3rdParty/pjproject/pjlib-util/include",
            "3rdParty/pjproject/pjnath/include",
            "3rdParty/pjproject/pjmedia/include",
            "include",
            "src",
            "src/pjmedia/include",
            "src/pjmedia/include/pjmedia",
            "3rdParty/rapidjson/include",
            "3rdParty/boost_1_66_0",
            "3rdParty/spandsp/src",
            "3rdParty/pocketsphinx/include",
            "3rdParty/pocketsphinx/build/include",
            "3rdParty/pjwebsock/websock",
            "<!@(node -p \"require('node-addon-api').include\")",
          ],
          'conditions': [
            [ 'OS!="win"', {
              'cflags': ['-g'],
              'cflags_cc': [
                '-g',
                '-fexceptions',
                '-Wno-maybe-uninitialized',
                '-fPIC',
              ],
              'ldflags_cc': [
                '-all-static',
              ]
              }
            ]
          ],
          'link_settings': {
              'libraries': [
                '-L ../3rdParty/pjproject/pjnath/lib',
                '-L ../3rdParty/pjproject/pjlib/lib',
                '-L ../3rdParty/pjproject/pjlib-util/lib',
                '-L ../3rdParty/pjproject/third_party/lib',
                '-L ../3rdParty/pjproject/pjmedia/lib',
                '-L ../3rdParty/pjproject/pjsip/lib',
                '-L ../3rdParty/pjproject/third_party/lib',
                '-l pjnath-x86_64-unknown-linux-gnu',
                '-l ilbccodec-x86_64-unknown-linux-gnu',
                '-l webrtc-x86_64-unknown-linux-gnu',
                '-l yuv-x86_64-unknown-linux-gnu',
                '-l speex-x86_64-unknown-linux-gnu',
                '-l gsmcodec-x86_64-unknown-linux-gnu',
                '-l g7221codec-x86_64-unknown-linux-gnu',
                '-l pjmedia-audiodev-x86_64-unknown-linux-gnu',
                '-l pjmedia-x86_64-unknown-linux-gnu',
                '-l resample-x86_64-unknown-linux-gnu',
                '-l pjmedia-codec-x86_64-unknown-linux-gnu',
                '-l pjmedia-videodev-x86_64-unknown-linux-gnu',
                '-l pjsdp-x86_64-unknown-linux-gnu',
                '-l pjsip-x86_64-unknown-linux-gnu',
                '-l pjsua2-x86_64-unknown-linux-gnu',
                '-l pjsip-ua-x86_64-unknown-linux-gnu',
                '-l pjsip-simple-x86_64-unknown-linux-gnu',
                '-l pjsua-x86_64-unknown-linux-gnu',
                '-l pj-x86_64-unknown-linux-gnu',
                '-l pjlib-util-x86_64-unknown-linux-gnu',
                '../3rdParty/spandsp/src/.libs/libspandsp.a',
                '../3rdParty/bcg729/src/libbcg729.a',
                '../3rdParty/pocketsphinx/build/libpocketsphinx.a',
                '-lstdc++',
                '-lopus',
                '-lssl',
                '-lcrypto',
                '-luuid',
                '-lm',
                '-ldl',
                '-ltiff',
                '-lrt',
                '-lpthread',
                '-lasound',
                '-lavformat',
                '-lavcodec',
                '-lswscale',
                '-lavutil',
                '-lspeex',
		'-lflite',
		'-lflite_cmu_us_awb',
		'-lflite_cmu_us_kal',
		'-lflite_cmu_us_rms',
		'-lflite_cmu_us_slt',
		'-lflite_cmu_us_kal16',
                '-l srtp-x86_64-unknown-linux-gnu',
              ],
          },
       },
    },

    {
      'target_name': 'addon',
      'type': 'loadable_module', # this is default for node-gyp but gyp will complain if absent.
      'dependencies': ['all-settings'],
    
      'actions': [
        {
          'action_name': 'build_deps',
          'message': 'executing build_deps.sh',
          'inputs': [],
          'outputs': ['./3rdParty'],
          'action': ['bash', './build_deps.sh'],
        },
      ],

      'sources': [
        'src/log.cpp',
        'src/event_templates.cpp',
        'src/idmanager.cpp',
        'src/sip.cpp',
        'src/addon.cpp',
        'src/pjmedia/src/pjmedia/dtmfdet.c',
        'src/pjmedia/src/pjmedia/bfsk_det.c',
        'src/pjmedia/src/pjmedia/fax_port.c',
        'src/pjmedia/src/pjmedia/flite_port.c',
        'src/pjmedia/src/pjmedia/pocketsphinx_port.c',
        'src/pjmedia/src/pjmedia/ws_speech_port.cpp',
        '3rdParty/pjwebsock/websock/http.c',
        '3rdParty/pjwebsock/websock/websock_transport_tcp.c',
        '3rdParty/pjwebsock/websock/websock_transport_tls.c',
        '3rdParty/pjwebsock/websock/websock.c',
        '3rdParty/pjwebsock/websock/websock_transport.c',
      ],
    },
  ],
}
