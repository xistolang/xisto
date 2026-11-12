# Xisto Debugger for VS Code

Runs `xisto dap`, the Xisto debug adapter. The program is built with `--debug`
(`-O0 -g`, statement hooks and the debugger agent) and debugged through the
Debug Adapter Protocol: breakpoints (conditions, hit counts, logpoints),
step over/into/out, call stack, variables (objects and arrays expand), hover,
watch of paths (`a.b[2].c`), stop on exceptions (all or uncaught), and
editing numbers, booleans, strings, enums and `null` while stopped.

## Install (from this folder)

```
cd editors/vscode
npx @vscode/vsce package      # makes xisto-debug-0.1.0.vsix
code --install-extension xisto-debug-0.1.0.vsix
```

Or, while developing, open this folder in VS Code and press F5 (Extension
Development Host).

Set `xisto.path` if `xisto` is not on `PATH` (for example
`/path/to/xisto/build/xisto`).

## launch.json

```json
{
  "type": "xisto",
  "request": "launch",
  "name": "Xisto: debug this file",
  "program": "${file}",
  "cwd": "${workspaceFolder}",
  "args": []
}
```

Without a launch.json, F5 on a `.ts` file debugs it.

## Attach to a running program

Start a `--debug` build with a port (127.0.0.1 only) and, optionally, your own
token (16 to 64 characters; otherwise one is generated and printed):

```
XISTO_DEBUG_PORT=9229 ./app
xisto: debugger listening on 127.0.0.1:9229 (token 3f9c...)
```

```json
{ "type": "xisto", "request": "attach", "name": "Xisto: attach", "port": 9229, "token": "3f9c..." }
```

Disconnecting leaves the program running.

## QBE backend

`"backend": "qbe"` builds with the QBE backend: breakpoints, steps, stack,
exceptions and variables work; variables are read-only there (QBE keeps
locals in registers), so editing needs the C backend (the default).
