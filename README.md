# CNS2 CarPlay Maps — Video Only

Free video-mirroring installer for the validated Volkswagen CNS2 275 / 0436 platform. The two instrument clusters have **separate USB packages**:

| Package | Cluster map area | Requested frame rate |
| --- | --- | --- |
| CNS2-790-VideoOnly-30FPS | 3GD920790, 1010 × 376 | 30 FPS |
| CNS2-791-VideoOnly-40FPS | 3G0920791, 800 × 480 | 40 FPS |

These are requested stream rates, not a guarantee of measured playback rate. Do not install a package for the other cluster.

## Included / not included

Included: CarPlay second-screen video producer, MOST video transport with native-map handback, key-authenticated SSH/SFTP maintenance on TCP 2222, same-unit backups and installation records.

**Not included:** music titles or album-art forwarding, navigation text / lane / turn-information patches, the HMI “activated on mobile device” message patch, startup animations, steering-wheel map controls, paid activation or device-bound encryption. Existing native functions are not a promise of these additional features. The original HMI launcher is never replaced.

## USB installation through Testmode

1. Download **one** matching package from Releases. Extract its contents directly to a FAT32 USB root: `delphi.sh`, `model.conf`, `runtime`, `payload`, `network`, and `profiles` must be at the root, not inside another folder.
2. Park the vehicle, keep stable external power, disconnect the iPhone, and do not run another installer.
3. Insert the USB and run `delphi.sh` through the existing Testmode engineering-menu script facility. This project does not unlock Testmode or install firmware. Do not run the vehicle scripts on a computer.
4. The installer validates the supported native binaries and the current unit, **installs/checks SSH first**, then stages and commits video. SSH failure blocks video installation. All steps must pass before the installer sends its single final reboot.
5. After boot, route the original map to the cluster first. Confirm its display and native zoom. Connect the iPhone and check video; unplug it and confirm the original map returns.

New USB flow and the new profile builds are **offline tested only** until a vehicle acceptance result is recorded. Offline checks do not prove visible playback, cold-boot timing, every phone, or every firmware revision. Stop rather than forcing an unsupported firmware/procman layout. No automatic downgrade of paid/unknown installations is provided.

## SSH access: read before installing

Installation explicitly enables **root SSH/SFTP access using the bundled maintenance public key**, for later owner-authorized support. The matching private key is **not distributed**. If you do not want that maintainer access, replace `network/cns275_sshd/etc/authorized_keys` with your own RSA public key **and regenerate `package-proof.txt` before installation**. Keep the private key off the USB and out of this repository.

Fresh installs generate a **unique host key on the headunit** and preserve it across updates. Only its public half is exported. Login authorization and host identity are different checks. Verify the exported host public key when connecting; do not disable host checking. No outbound tunnel or automatic contact with a remote server is installed.

The installer checks key-only configuration, public-key parsing, the exact running listener and its TCP 2222 SSH banner. **This is not a completed remote private-key login test.** Test the actual client login after replacing the installer USB with the Ethernet adapter. Default wired headunit address: `172.16.250.248/24`; a directly attached PC can use `172.16.250.123/24`. No Wi-Fi connection is enabled. Do not expose this legacy SSH port directly to the Internet; use a controlled private maintenance network.

This free package does not blanket-stop `inetd`, disable networking, close existing Telnet services, change the root password, or install a 250 ms integrity guard. Existing third-party listeners are outside its scope.

## Records and later upgrades

The USB receives `cns2-free-install-latest.log`, a generation-specific log and the **public** SSH host key. Same-unit records also stay under `/persistence/cns275/install-records/<manufacturer-ID>/`. Records contain the local unit ID and installation contents; do not upload customer records to GitHub.

Persistent startup paths intentionally match the current encrypted installer: `.v43c`, `.v43m`, `.v43a`, `.v43l`, `.v43b`, `/persistence/pm.cfg`, and the four short entrypoints. They are **compatibility paths**, not paid features. New free-on-free updates use project-only maintenance; no SSH/network/native process is killed. Paid upgrades must still perform their own baseline, same-unit and maintenance checks; matching paths alone do not prove a successful paid upgrade.

## Source, build and licenses

Project-owned sources/scripts are under MIT; third-party components retain their own terms. See [LICENSE](LICENSE), [third-party notices](THIRD_PARTY_NOTICES.md), [build notes](docs/BUILD.md) and [recovery notes](docs/RECOVERY.md). No OEM firmware image, OEM HMI JAR, SDK, license file, private key, signing secret or customer installation data is included. Volkswagen, Škoda, Apple and CarPlay names are identification only; no affiliation or endorsement is implied.

This software is supplied as-is. Use only on hardware you own or are authorized to service, while stationary. Maintain a recovery route and comply with applicable safety and local requirements. Repository privacy is deliberately unchanged; making it public is a separate owner decision.

---

中文简要说明：790、791 各有一个独立 U 盘包，分别请求 30 帧、40 帧。解压到 U 盘根目录，通过 Testmode 工程菜单运行 `delphi.sh`。严格先装并检查 SSH，成功后才装投屏；只做投屏，不带音乐、导航信息、提示文字补丁、动画或方向盘控制。全部通过后只重启一次。新包仅完成离线验证，尚须静止实车验收；不要将离线测试写成实车通过。
