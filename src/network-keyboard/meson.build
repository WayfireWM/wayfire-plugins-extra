boost = dependency('boost')
pthread = dependency('threads')
gtkmm = dependency('gtkmm-3.0')
xkbcommon = dependency('xkbcommon')

nkserverexe = executable('wf-nk-server', 'server.cpp',
    dependencies: [wayfire, boost, pthread, gtkmm, wf_protos], install: true)

nkclientexe = executable('wf-nk-client', ['client.cpp', 'shared/os-compatibility.c'],
    dependencies: [wayfire, boost, pthread, wf_protos, xkbcommon], install: true)

