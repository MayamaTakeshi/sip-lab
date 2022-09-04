{
  'targets': [
    {
      'target_name': 'addon',
      'sources': [
        'src/log.cpp',
        'src/event_templates.cpp',
        'src/idmanager.cpp',
        'src/packetdumper.cpp',
        'src/sip.cpp',
        'src/addon.cpp',
        'src/pjmedia/src/chainlink/chainlink_dtmfdet.c',
        'src/pjmedia/src/chainlink/chainlink_tonegen.c',
        'src/pjmedia/src/chainlink/chainlink_wav_player.c',
        'src/pjmedia/src/chainlink/chainlink_wav_writer.c',
        'src/pjmedia/src/chainlink/chainlink_wire_port.c',
        'src/pjmedia/src/chainlink/chainlink_fax.c',
      ],
      'include_dirs': [
        "pjproject/pjsip/include",
        "pjproject/pjlib/include",
        "pjproject/pjlib-util/include",
        "pjproject/pjnath/include",
        "pjproject/pjmedia/include",
        "include",
        "src",
        "src/pjmedia/include",
        "src/pjmedia/include/pjmedia",
        "src/pjmedia/include/chainlink",	
        "rapidjson/include",
        "<!@(node -p \"require('node-addon-api').include\")",
      ],
      "dependencies": [
        "<!@(node -p \"require('node-addon-api').gyp\")"
      ],
      'conditions': [
        [ 'OS!="win"', {
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
            '-L ../pjproject/pjnath/lib',
            '-L ../pjproject/pjlib/lib',
            '-L ../pjproject/pjlib-util/lib',
            '-L ../pjproject/third_party/lib',
            '-L ../pjproject/pjmedia/lib',
            '-L ../pjproject/pjsip/lib',
            '-L ../pjproject/third_party/lib',
            '-L ../bcg729/src',
            '-l pjnath-x86_64-unknown-linux-gnu',
            '-l ilbccodec-x86_64-unknown-linux-gnu',
            '-l srtp-x86_64-unknown-linux-gnu',
            '-l webrtc-x86_64-unknown-linux-gnu',
            '-l yuv-x86_64-unknown-linux-gnu',
            '-l speex-x86_64-unknown-linux-gnu',
            '-l gsmcodec-x86_64-unknown-linux-gnu',
            '-l g7221codec-x86_64-unknown-linux-gnu',
            '-l resample-x86_64-unknown-linux-gnu',
            '-l pjmedia-audiodev-x86_64-unknown-linux-gnu',
            '-l pjmedia-codec-x86_64-unknown-linux-gnu',
            '-l pjmedia-videodev-x86_64-unknown-linux-gnu',
            '-l pjmedia-x86_64-unknown-linux-gnu',
            '-l pjsdp-x86_64-unknown-linux-gnu',
            '-l pjsip-x86_64-unknown-linux-gnu',
            '-l pjsua2-x86_64-unknown-linux-gnu',
            '-l pjsip-ua-x86_64-unknown-linux-gnu',
            '-l pjsip-simple-x86_64-unknown-linux-gnu',
            '-l pjsua-x86_64-unknown-linux-gnu',
            '-l pj-x86_64-unknown-linux-gnu',
            '-l pjlib-util-x86_64-unknown-linux-gnu',
            '-lstdc++',
            '-lopus',
            '-lssl',
            '-lcrypto',
            '-luuid',
            '-lm',
            '-ldl',
            '-lspandsp',
            '-lpcap',
            '-lrt',
            '-lpthread',
            '-lasound',
            '-lSDL2',
            '-lavdevice',
            '-lavformat',
            '-lavcodec',
            '-lswscale',
            '-lavutil',
            '-lv4l2',
            '-lopencore-amrnb',
            '-lopencore-amrwb',
            '-lvo-amrwbenc',
            '-lspeex',
            '-lbcg729',
          ],
      },
    },
  ]
}
