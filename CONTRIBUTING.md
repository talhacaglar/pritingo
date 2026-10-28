# Contributing to Pritingo

Thanks for helping. Issues and pull requests are welcome.

- **Bugs**: open an issue with what you did, what you expected, and what
  happened. If printing failed, include `lpstat -t` and
  `journalctl -u cups --since -10min`.
- **Other printers**: the driver is wired in `src/backend.cpp`
  (`driverPpd()` and the `pstocanonij` call in `sendItem()`). A change that
  picks the filter and PPD per queue, or adds a plain CUPS path for printers
  that handle multi-page jobs, is the most useful contribution right now.
- **Translations**: copy `src/i18n/tr.json`, translate the values (keep `%1`
  and `%n`), save it as `src/i18n/<code>.json` with an Omarchy language code,
  and add it to `src/resources.qrc`.

Before opening a pull request:

```sh
bin/build
bin/test
```

Keep logic in C++ (`src/backend.cpp`, `src/planner.cpp`, with tests in
`tests/`) and layout in QML. Comments explain why, in full sentences.
Commit messages are short and imperative ("Support the E480").
