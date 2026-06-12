# sketches — .ino entry points

Sketches are thin consumers of the library: identity + registration + hooks,
then `start`/`service`. No protocol behavior lives here. Read the
`arduino-cli-builds` skill before adding one.

Planned (backlog Phase 9/10):

```text
ethernet_smoke/                  link bring-up + listen, no command server
command_server_conformance/      full library image the Python conformance
                                 client (tests/conformance/) runs against
```

`./tools/build_teensy.sh` compiles every `sketches/<name>/<name>.ino`. Any
`.ino` anywhere else under `sketches/` (wrong basename, deeper nesting) fails
the build with a layout error — before the toolchain check, so the error is
visible even without arduino-cli installed.
