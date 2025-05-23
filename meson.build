project(
	'wayfire-plugins-extra',
	'c',
	'cpp',
	version: '0.10.0',
	license: 'MIT',
	meson_version: '>=0.51.0',
	default_options: [
		'cpp_std=c++17',
		'c_std=c11',
		'warning_level=2',
		'werror=false',
	],
)

wayfire = dependency('wayfire', version: '>=0.9.0')
giomm = dependency('giomm-2.4', required: false)
wayland_protos = dependency('wayland-protocols', version: '>=1.12')
wayland_server = dependency('wayland-server')
evdev = dependency('libevdev')
json = dependency('yyjson', required: false)

if get_option('enable_wayfire_shadows') == true
    wayfire_shadows = subproject('wayfire-shadows')
endif

if get_option('enable_focus_request') == true
    focus_request = subproject('focus-request')
endif

if get_option('enable_pixdecor') == true
    pixdecor = subproject('pixdecor')
endif

if get_option('enable_filters') == true
    filters = subproject('filters')
endif

add_project_arguments(['-DWLR_USE_UNSTABLE'], language: ['cpp', 'c'])
add_project_arguments(['-DWAYFIRE_PLUGIN'], language: ['cpp', 'c'])
add_project_link_arguments(['-rdynamic'], language:'cpp')

add_project_arguments(['-Wno-unused-parameter'], language: 'cpp')
subdir('proto')
subdir('src')
subdir('metadata')

summary = [
	'',
	'----------------',
	'wayfire-plugins-extra @0@'.format(meson.project_version()),
	'----------------',
	''
]
message('\n'.join(summary))
