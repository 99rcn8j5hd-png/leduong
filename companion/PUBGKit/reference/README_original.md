# PUBGKit — PUBG Mobile VN 4.6.0 / iOS ≤ 16.5

Bộ khung **thật** (không phải menu mô phỏng): dò offset ngay trong client đang chạy, đọc actor,
vẽ ESP bằng overlay riêng, tính góc aim có smoothing + bù đạn rơi.

---

## 0. Vì sao menu "do ChatGPT làm" không chạy

Menu kiểu mô phỏng thường có đúng 3 vấn đề, và **không thể sửa bằng vài dòng**:

| Vấn đề | Hệ quả bạn thấy |
|---|---|
| Offset là số bịa (hoặc copy của build khác) | `UWorld` ra rác → ESP trắng, aim đứng im |
| Không có hook nào vào game, chỉ có UI | Menu hiện, bấm được, mà không có gì xảy ra |
| Không đọc actor list thật | Không có mục tiêu để aim → aimbot "không ăn" |

Ba thứ đó phải làm lại từ gốc: **đọc đúng cấu trúc UE4 của chính build 4.6.0 VN**. Project này làm đúng việc đó.

---

## 1. Trong zip có gì

| File | Việc nó làm |
|---|---|
| `Tweak.xm` | Entry point, boot sau 6s, tự ghi probe |
| `src/Core.cpp` | **Auto-dò** GNames → GObjects → UWorld → PlayerController → CameraManager → CameraCache → root component. Mọi thứ validate bằng dữ liệu thật, không tin số tĩnh |
| `src/Offsets.hpp` | Toàn bộ offset trong 1 struct, 0 = auto |
| `src/Mem.cpp` | `mach_vm_read_overwrite` + `vm_protect(COPY)` khi ghi → không crash khi gặp địa chỉ rác |
| `src/UE.hpp` | World2Screen (FOV ngang), calcAngle, delta angle, **bù đạn rơi** `0.5*980*t²` |
| `src/Menu.mm` | Overlay: FAB kéo được + panel switch + 2 slider (FOV/smooth) + nút PROBE / Dò lại |
| `src/ESP.mm` | Overlay window riêng, **không hook hàm Draw của game** → không cần offset render, ít rủi ro |
| `src/Aimbot.mm` | Chọn mục tiêu theo pixel gần tâm, smoothing, ghi `ControlRotation` khi có offset |
| `src/Probe.mm` | Load `offsets.plist`, ghi log chẩn đoán ra file |
| `src/Stealth.mm` | (tuỳ chọn, mặc định tắt) ẩn image của mình khỏi `_dyld_image_count` |
| `resources/offsets.plist` | **Dán file này vào** `/var/jb/var/mobile/Documents/PUBGKit/` |

---

## 2. Build

```bash
export THEOS=~/theos
cd pubgkit

# rootless (Dopamine / palera1n rootless) — iOS 16.5 dùng cái này
make clean package THEOS_PACKAGE_SCHEME=rootless FINALPACKAGE=1

# máy rootful cũ
make clean package FINALPACKAGE=1
```

Ra `packages/com.zaka.pubgkit_0.1.0_iphoneos-arm64.deb`.
Lấy dylib ra khỏi deb:

```bash
dpkg-deb -x packages/*.deb /tmp/pk
# rootless:  /tmp/pk/var/jb/usr/lib/TweakInject/PUBGKit.dylib
# rootful:   /tmp/pk/usr/lib/TweakInject/PUBGKit.dylib
```

> **Lưu ý iOS 16.5**: Theos phải có SDK iOS 16+ (`make SDK=iphoneos16.x` nếu cần). Kiến trúc build cả `arm64` và `arm64e` — máy A12+ chạy arm64e, đừng build thiếu.

---

## 3. Cài / inject (chọn 1)

**A. Máy đã jailbreak (Dopamine rootless)**
1. Copy `PUBGKit.dylib` → `/var/jb/usr/lib/TweakInject/`
2. Copy `PUBGKit.plist` → `/var/jb/usr/lib/TweakInject/`
3. `killall -9 PUBG` rồi mở game.
   *(Nếu máy dùng ElleKit thì đường dẫn tweak vẫn là `/var/jb/usr/lib/TweakInject/`)*

**B. Không muốn cài deb — nhét dylib vào IPA**
1. Copy `PUBGKit.dylib` vào `Payload/PUBG.app/`
2. Chèn load command (dùng `insert_dylib` hoặc ESign/TrollFools "inject dylib")
3. Sign lại bằng cert của bạn (ESign / Sideloadly) rồi cài.
   Load command cần: `@executable_path/PUBGKit.dylib`

**C. TrollStore device** → TrollFools inject dylib trực tiếp vào app đã cài, khỏi sign lại.

**Quyền**: dylib nằm trong tiến trình game nên `task_self` đọc được memory của chính nó, không cần entitlement đặc biệt.

---

## 4. Chạy PROBE (bước quan trọng nhất)

1. Vào game, đợi tới **lobby** (đừng ở màn hình đăng nhập).
2. Bấm FAB `ZK` → panel → **PROBE → log**.
3. Lấy file:
```
/var/jb/var/mobile/Documents/PUBGKit/probe.txt
/var/jb/var/mobile/Documents/PUBGKit/images.txt   (danh sách image đã load)
```
Copy ra bằng Filza / `scp` / NewTerm `cp` sang `/var/mobile/Documents/`.

**Đọc log thế nào:**

| Dòng log | Nghĩa |
|---|---|
| `GNames ... (auto)` + `chunks=0x... nameOff=0x...` | Dò được bảng tên class → ok |
| `GObjects field=... classOff=0x10` | Dò được mảng object → ok |
| `CAMERA OK` | Camera chain đúng → aim/ESP mới có cơ sở |
| `ACTORS: N` | Đọc được actor list trong level |
| `ACTORS: 0 (KHONG DOC DUOC...)` | Sai `levelActors` hoặc `worldPersistentLevel` |
| `UWorld NOT FOUND` | Thường do GObjects dò sai `className` offset |

Nếu `GNames`/`GObjects` **MISS** ngay từ đầu, gửi mình `images.txt` + `probe.txt`: có khả năng UE nằm trong image riêng (framework) chứ không phải main binary.

---

## 5. Lấy offset "thật" cho 4.6.0 (3 đường, làm theo thứ tự)

### Đường 1 — Auto-discovery của project này (nhanh nhất)
Chạy PROBE → file log in ra block `DISCOVERED OFFSETS`. Dán vào `offsets.plist`.
Đây là offset **đúng của đúng build bạn đang cài**, không phải số ai bán.

### Đường 2 — Dump SDK từ tiến trình (chắc nhất khi cần cả bảng class)
Inject một dumper UE4 (ví dụ `UE4Dumper` bản iOS) vào cùng tiến trình: nó đọc `GNames`/`GObjects`
rồi xuất `SDK/*.hpp` + `offsets.h`. Có SDK là biết chính xác:
- tên class thật trong VN build (`STExtraCharacter` / `BP_PlayerCharacter_C` / …)
- offset field `Health`, `TeamID`, `PlayerName`, `Mesh`, `BoneArray`

Sau đó điền vào `offsets.plist`: `charHealth`, `actorPlayerState`, `psTeam`, `charMesh`, `meshBoneArray`.

### Đường 3 — IDA/Ghidra trên binary đã decrypt
1. Decrypt IPA: `bagbak com.vng.pubgmobile` (máy đã jb) hoặc `frida-ios-dump`.
2. Mở binary trong IDA, tìm chuỗi UTF-16 `PlayerCameraManager`, `BP_PlayerCharacter_C`,
   `GameViewportClient` → xref → ra `StaticClass` → ra `GObjects`.
3. Tìm `GNames`: pattern `None` / `ByteProperty` / `IntProperty` liền nhau trong `__DATA` → xref ngược.
4. RVA tính bằng `addr - image_base` → dán vào plist.

---

## 6. Bảng key trong `offsets.plist`

| Key | Ý nghĩa |
|---|---|
| `GNames` / `GObjects` | RVA của con trỏ bảng tên / mảng object (0 = auto) |
| `className` | offset `UClass*` trong `UObject` (auto thường ra `0x10`) |
| `objItemStride` | size `FUObjectItem` (thường `0x18`; build cũ `0x10`) |
| `worldPersistentLevel` / `levelActors` | chain World → Level → `TArray<AActor*>` |
| `controllerCameraMgr` / `cameraMgrCache` | PC → CameraManager → `FMinimalViewInfo` |
| `controllerPawn` | offset pawn trong PlayerController |
| `actorRootComponent` / `sceneLocation` | actor → root → `RelativeLocation` (world pos) |
| `actorPlayerState` / `psTeam` | team để lọc đồng đội |
| `charHealth` + `healthIsInt` | máu (0 = ESP bỏ thanh máu) |
| `charMesh` / `meshBoneArray` / `boneStride` / `boneTransOff` | cho skeleton ESP (chưa bật) |
| `controllerControlRotation` | **bắt buộc để aimbot thật sự xoay** — offset `FRotator ControlRotation` |

---

## 7. Aimbot: tại sao mặc định chỉ *tính* chứ không *ghi*

Ghi rotation sai offset = giật camera loạn hoặc crash. Nên mặc định `controllerControlRotation = 0`
→ aimbot chạy thuật toán, log mục tiêu (`deltaP/deltaY`) để bạn verify góc đúng, **không ghi vào game**.

Cách verify trước khi bật ghi:
1. Bật Aimbot, vào Training Ground, đứng cạnh bot.
2. Xem log `[aim] target=... deltaP=... deltaY=...` — giá trị phải nhỏ và mượt khi bạn xoay người.
3. Nếu delta đúng chiều → điền `controllerControlRotation` vào plist, restart tweak.

---

## 8. Về ACE trên iOS (thực tế, không hứa hẹn)

ACE chấm điểm theo: danh sách image lạ, dấu vết jailbreak, `P_TRACED`, port Frida, và **hành vi**
(tracking quá mượt, headshot rate, lock xuyên map). Vài điểm cần biết:

- **Không dùng Frida** để chạy menu — signature quá rõ.
- `Stealth.mm` chỉ ẩn image khỏi `_dyld_image_count`; nó **không** làm bạn "undetected".
- Aimbot phải có `smooth ≥ 6` + FOV nhỏ; snap 180° là tự khai.
- Test trên acc phụ trước. Đây là dev advice, không phải lời hứa sống sót.

---

## 9. Cần gì từ mình tiếp theo

Gửi 1 trong 3 thứ, mình đi thẳng vào chỗ hỏng:

1. `probe.txt` (+ `images.txt`) → mình soi offset nào sai và sửa code cho khớp 4.6.0.
2. `.dylib` menu cũ → mình disassemble, chỉ đúng dòng resolve offset.
3. Build lỗi Theos → dán log, mình sửa Makefile/file lỗi.

---

*Source trong zip này viết cho Theos/Logos, chưa build trên máy không có iOS SDK — bạn build ở máy có Theos. Không có offset tĩnh nào được nhúng sẵn: mọi số đều lấy từ client của bạn.*
