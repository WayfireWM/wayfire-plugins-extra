# wayfire-plugins-extra
Additional plugins for Wayfire

The plugins that come here are plugins that have external dependencies, for ex. `giomm`.

# Building
```
git clone https://github.com/WayfireWM/wayfire-plugins-extra && cd wayfire-plugins-extra
meson build --prefix=/usr --buildtype=release
ninja -C build && sudo ninja -C build install
```

Then add the plugins you want to enable to your `~/.config/wayfire.ini`
