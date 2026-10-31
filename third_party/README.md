# Third-party sources kept in this repository

Most of what the build needs is fetched at a pinned version (`tools/deps.json`, `make deps`). These
are kept here instead, as small trimmed copies with PS5 changes of their own:

- `libsmb2/`: the SMB2/3 client library (LGPL-2.1-or-later), and
- `libnfs/`: the NFS client library (LGPL-2.1-or-later; its protocol files BSD-2-Clause),

both as Porpoise X carries them (premohq/Porpoise-X, `third_party/`, commit
`2ed1ab5a06995aa198f7bccd4da0f90c3fc29d39`): the client libraries only, each with its PS5
configuration (`ps5/config.h`), the list of files the build compiles (`sources.txt`) and its changes
from upstream (`README.porpoise`). The app reads games on a network share with them
(`port/app/netfs.cpp`). They call back into the app by the names Porpoise
gave those hooks (`porpoise_netfs_*`), kept so the copies stay Porpoise's. The build
(`port/CMakeLists.txt`) renames libsmb2's ciphers and hashes (`AES128_ECB_encrypt`, `MD5Init`,
`hmac` and the like) to `smb2_` names rather than editing the copy: Cemu has an
`AES128_ECB_encrypt` of its own (#46).
