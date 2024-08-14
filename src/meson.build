annotate = shared_module('annotate', 'annotate.cpp',
    dependencies: [wayfire],
    install: true, install_dir: join_paths(get_option('libdir'), 'wayfire'))

if giomm.found()
autorotate = shared_module('autorotate-iio', 'autorotate-iio.cpp',
    dependencies: [wayfire, giomm],
    install: true, install_dir: join_paths(get_option('libdir'), 'wayfire'))
endif

benchmark = shared_module('bench', 'bench.cpp',
    dependencies: [wayfire],
    install: true, install_dir: join_paths(get_option('libdir'), 'wayfire'))

crosshair = shared_module('crosshair', 'crosshair.cpp',
    dependencies: [wayfire],
    install: true, install_dir: join_paths(get_option('libdir'), 'wayfire'))

focus_change = shared_module('focus-change', 'focus-change.cpp',
    dependencies: [wayfire],
    install: true, install_dir: join_paths(get_option('libdir'), 'wayfire'))

focus_steal_prevent = shared_module('focus-steal-prevent', 'focus-steal-prevent.cpp',
    dependencies: [wayfire, evdev],
    install: true, install_dir: join_paths(get_option('libdir'), 'wayfire'))

follow_focus = shared_module('follow-focus', 'follow-focus.cpp',
    dependencies: [wayfire],
    install: true, install_dir: join_paths(get_option('libdir'), 'wayfire'))

force_fullscreen = shared_module('force-fullscreen', 'force-fullscreen.cpp',
    dependencies: [wayfire],
    install: true, install_dir: join_paths(get_option('libdir'), 'wayfire'))

ghost = shared_module('ghost', 'ghost.cpp',
    dependencies: [wayfire],
    install: true, install_dir: join_paths(get_option('libdir'), 'wayfire'))

if giomm.found()
glib_main_loop = shared_module('glib-main-loop', 'glib-main-loop.cpp',
    dependencies: [wayfire, giomm],
    install: true, install_dir: join_paths(get_option('libdir'), 'wayfire'))
endif

hide_cursor = shared_module('hide-cursor', 'hide-cursor.cpp',
    dependencies: [wayfire],
    install: true, install_dir: join_paths(get_option('libdir'), 'wayfire'))

joinviews = shared_module('join-views', 'join-views.cpp',
    dependencies: [wayfire],
    install: true, install_dir: join_paths(get_option('libdir'), 'wayfire'))

keycolor = shared_module('keycolor', 'keycolor.cpp',
    dependencies: [wayfire],
    install: true, install_dir: join_paths(get_option('libdir'), 'wayfire'))

magnifier = shared_module('mag', 'mag.cpp',
    dependencies: [wayfire],
    install: true, install_dir: join_paths(get_option('libdir'), 'wayfire'))

if json.found()
obs = shared_module('obs', 'obs.cpp',
    dependencies: [wayfire, json],
    install: true, install_dir: join_paths(get_option('libdir'), 'wayfire'))
pin_view = shared_module('pin-view', 'pin-view.cpp',
    dependencies: [wayfire, json],
    install: true, install_dir: join_paths(get_option('libdir'), 'wayfire'))
endif

showrepaint = shared_module('showrepaint', 'showrepaint.cpp',
    dependencies: [wayfire],
    install: true, install_dir: join_paths(get_option('libdir'), 'wayfire'))

view_shot = shared_module('view-shot', 'view-shot.cpp',
    dependencies: [wayfire],
    install: true, install_dir: join_paths(get_option('libdir'), 'wayfire'))

water = shared_module('water', 'water.cpp',
    dependencies: [wayfire],
    install: true, install_dir: join_paths(get_option('libdir'), 'wayfire'))

window_zoom = shared_module('winzoom', 'window-zoom.cpp',
    dependencies: [wayfire],
    install: true, install_dir: join_paths(get_option('libdir'), 'wayfire'))

workspace_names = shared_module('workspace-names', 'workspace-names.cpp',
    dependencies: [wayfire],
    install: true, install_dir: join_paths(get_option('libdir'), 'wayfire'))
