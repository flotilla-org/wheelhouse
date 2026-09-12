This lockfile pins Wheelhouse's embedded Andamento dependencies. The build
helper generates a Cargo consumer workspace under `build/andamento`, pointing
at the configured Andamento checkout. Only `andamento-ffi` and its dependencies
are resolved; Andamento's Zellij workspace members are not required.

When updating Andamento, regenerate the workspace and deliberately refresh its
lockfile if needed:

```sh
python3 tools/prepare-andamento-build.py ../andamento
cargo update --manifest-path build/andamento/Cargo.toml
cp build/andamento/Cargo.lock tools/andamento-build/Cargo.lock
```

Normal builds use `--locked` and fail if the committed lockfile needs updating.
