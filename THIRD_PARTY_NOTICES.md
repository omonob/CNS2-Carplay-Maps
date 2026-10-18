# Third-party notices

Project source and scripts: copyright 2026 omonob, MIT, except preserved
third-party headers/notices. No proprietary toolchain or OEM firmware is
licensed by this project's MIT file.

- Monocypher 4.0.3: copyright Loup Vaillant and contributors, BSD-2-Clause or
  CC0, original notices retained in `source/vendor` and
  `licenses/MONOCYPHER-LICENCE.md`.
- The legacy QNX SSH binaries identify as OpenSSH 5.2
  `QNX_Secure_Shell-20090621` / HPN13v6. They are unmodified maintenance
  binaries, not project-owned code. Preserve the OpenSSH license collection
  in `licenses/OPENSSH-LICENCE.txt` and the upstream attribution. The upstream
  source is https://github.com/openssh/openssh-portable/tree/V_5_2_P1 ; this
  source link is not a claim that the QNX vendor port is reproducibly built.
- The included cryptographic library identifies as OpenSSL 1.0.2j.
  Its OpenSSL/SSLeay terms remain in `licenses/OPENSSL-LICENSE.txt`.
  This product includes software developed by the OpenSSL Project for use
  in the OpenSSL Toolkit (http://www.openssl.org/).
  This product includes cryptographic software written by Eric Young
  (eay@cryptsoft.com). This product includes software written by Tim Hudson
  (tjh@cryptsoft.com).

These old network components are provided for isolated legacy-unit maintenance,
not as a modern general-purpose Internet-facing SSH service. A public release
must preserve these notices and review the provenance/redistribution terms of
any replacement port. No QNX SDK libraries, license files or OEM application
binaries are included. Native firmware proof lists contain only paths, sizes
and digests; the startup delta applies on the owning unit only after an exact
native-file check.
