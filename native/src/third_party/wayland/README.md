# libwayland client headers

`wayland-client-core.h`, `wayland-util.h` and `wayland-version.h` from
[wayland](https://gitlab.freedesktop.org/wayland/wayland) 1.26.0, MIT
licensed (the notice is at the top of each file). They let the Wayland window
manager build without libwayland's development package. It loads
libwayland-client with `dlopen` at run time and needs version 1.20 or later,
for `wl_proxy_marshal_flags`.
