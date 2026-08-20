# VirtualDub behavior contract

`parity_contract_tests` turns a small set of deterministic editor operations
into JSON and compares the result with `virtualdub2-reference.json`.

The checked-in contract covers the command form emitted by VirtualDub2's
`Job.cpp`, edit-list mapping (including masked ranges), representative video
filter behavior, and audio sample processing. It is deliberately independent
of codecs and hardware so it runs on every build.

To inspect or capture the native result:

```bash
./build/parity_contract_tests --emit
```

To compare against another reference capture:

```bash
./build/parity_contract_tests --reference path/to/reference.json
```

New parity work should add the smallest deterministic case that distinguishes
the VirtualDub2 behavior being ported. A reference should only be changed after
the corresponding behavior has been checked against the bundled original
source or a Windows VirtualDub2 result.
