# PUBGKit — bộ dò GNames / GObjects cho PUBG Mobile VN 4.6.0 (iOS 15–16.5)

Mục tiêu: **xác định GNames (FNamePool) và GObjects ngay trong tiến trình game**, không cần
offset tĩnh, không cần binary đã decrypt, không phụ thuộc bản build.

---

## 1. Vì sao ChatGPT bó tay ở đoạn này

Nó tìm `GNames` như tìm một **symbol / chuỗi** trong file `ShadowTrackerExtra` 179 MB. Cách đó sai
về bản chất, 3 lý do cứng:

1. **`cryptid=1`** → `__TEXT` bị mã hoá FairPlay. Chỉ 4 KB header là plaintext, phần code đã mã hoá.
   Đọc ra chỉ thấy rác, mọi "tìm symbol" đều vô nghĩa. Muốn static analysis thì **phải decrypt**
   bằng frida-ios-dump / bagbak trên máy đã jailbreak.
2. **GNames không phải symbol, mà là một con trỏ runtime.** Từ UE4.18 trở đi nó là `FNamePool`:
   một global object trong `__DATA` (`.bss`, giá trị 0 trong file), bên trong là mảng con trỏ
   `Blocks[]` trỏ tới các block 64 KB cấp phát lúc chạy. Trong file **không có chữ "GNames"**,
   trong bảng symbol cũng không (build release strip sạch).
3. **Tên class cũng không nằm trong `__cstring`.** Mọi `FName` (PlayerCameraManager,
   BP_PlayerCharacter_C…) được **dựng vào pool lúc runtime**. Nên grep chuỗi "None" hay
   "PlayerCameraManager" trong binary → không kết luận được gì, đúng như cái note trong ảnh bạn gửi.

→ Vì vậy không đi đường file. Đi đường **runtime**: quét memory của chính process game, nhận diện
`FNamePool` bằng cấu trúc + validate bằng cách decode tên thật. Đây là cách các menu sống qua
được update.

---

## 2. Công cụ này làm gì

| File | Việc |
|---|---|
| `src/dyld_util.hpp` | `vm_read_overwrite` an toàn (không crash khi gặp địa chỉ rác), liệt kê vùng RW, tìm base của `ShadowTrackerExtra` |
| `src/fname.cpp` | **Dò FNamePool**: quét vùng RW tìm entry `None` (`08 00 'N''o''n''e'`), walk chuỗi entry liên tiếp để loại false positive, rồi tìm mảng `Blocks[]` trỏ về block đó. Decode `FName` theo `id = (block<<16) \| byteOffset` |
| `src/objects.cpp` | **Dò GObjects** (`TUObjectArray`): validate `NumElements/MaxElements/NumChunks` + thử `itemSize` 0x10 / 0x18, xác nhận bằng cách đọc `ClassPrivate (+0x10)` → `NamePrivate (+0x18)` rồi decode tên class |
| `src/probe.mm` | Chạy sau ~12 s, ghi `zkprobe.txt`: base, slide, RVAs, dump struct quanh `Blocks[]`, tổng số tên, id các tên quan trọng, và danh sách instance (`World`, `PlayerController`, `PlayerCameraManager`…) |

Không có một con số offset nào bị bịa trong code. Cái gì chưa chắc thì nó **in ra để bạn kiểm tra**,
không nhét vào plist.

---

## 3. Cách nhanh nhất — Frida (không cần build gì)

Nếu máy đã có Frida server:

```bash
frida -U -f com.vng.pubgmobile -l frida/probe.js --no-pause
# vào tới lobby, rồi gõ trong console:  zkProbe()
```

`frida/probe.js` làm y hệt bản dylib: scan `08 00 4E 6F 6E 65` trong các range `rw-` → validate
bằng walk entry → tìm mảng `Blocks[]` → dump id các tên quan trọng → dò `GObjects` → in RVAs.

Dùng Frida để **dò** thì nhanh, nhưng đừng dùng Frida để **chạy menu** (signature quá rõ, xem mục 8).

---

## 3b. Build dylib (nếu muốn chạy tĩnh, không Frida)

Cách A — macOS + Xcode (khuyến nghị):

```bash
cd pubg-gnames46
./build.sh
# -> PUBGKit.dylib (arm64 + arm64e)
```

Cách B — Theos (trên Mac hoặc trên máy jb qua Theos-on-device):

```make
TARGET := iphone:clang:latest:15.0
ARCHS  := arm64 arm64e
include $(THEOS)/makefiles/common.mk

LIBRARY_NAME = PUBGKit
PUBGKit_FILES = src/fname.cpp src/objects.cpp src/probe.mm
PUBGKit_FRAMEWORKS = Foundation UIKit
PUBGKit_CFLAGS = -Isrc -fobjc-arc
include $(THEOS_MAKE_PATH)/library.mk
```

> Máy A12+ chạy app arm64 vẫn ok, nhưng build **cả 2 arch** cho chắc — thiếu slice là dyld không load.

---

## 4. Cài vào app

Chọn 1 trong 3:

1. **TrollFools / Filza + ElleKit (rootless)**: chép `PUBGKit.dylib` vào
   `/var/jb/usr/lib/TweakInject/` hoặc dùng TrollFools inject trực tiếp vào app `com.vng.pubgmobile`.
2. **ESign / Feather / TrollStore**: nhét `PUBGKit.dylib` vào `Payload/PUBG.app/`, rồi
   `insert_dylib @executable_path/PUBGKit.dylib Payload/PUBG.app/PUBG` → ký lại toàn bộ
   (nhớ ký cả binary trong bundle, không chỉ .app).
3. **Theos tweak riêng** load dylib này như library.

---

## 5. Chạy và lấy kết quả

1. Mở game, **vào tới lobby** (đừng ở màn đăng nhập — lúc đó pool chưa dựng xong).
2. Chờ ~12 s (probe tự chạy). Muốn chạy lại:
   - Frida: `frida -U -n PUBG -e "Module.findExportByName(null,'zkProbe').readByteArray()"` rồi gọi
     `new NativeFunction(Module.findExportByName(null,'zkProbe'),'void',[])()`
   - hoặc cứ khởi động lại app.
3. Lấy log: `zkprobe.txt` — ở `/var/mobile/Documents/` (Filza) hoặc trong Documents của app.
   Xem nhanh bằng console: `idevicesyslog | grep ZK` (nó cũng `NSLog`).

Log sẽ có dạng:

```
================ ZK PROBE ================
[env] mainBase = 0x104abc000   slide = ...
[names] blocks  = 0x...
[names] rvaBlocks = 0x...      (FNamePool::Blocks = base + rva nay)
[names] rvaPool   = 0x...      (struct FNamePool, neu menu cua ban can)
[names] total names = 180000+
[names]   PlayerCameraManager  id=0x0001a3c4 OK
[objs] rvaArray  = 0x...
[objs] numElements = 300000+   itemSize = 0x18
[objs]   [ 1234] World            World            0x...
[objs]   [ 5678] PlayerController BP_PlayerController_C_2147...  0x...
===== DISCOVERED OFFSETS (dan vao plist) =====
GNAMES_BLOCKS_RVA = 0x...
GOBJECTS_RVA      = 0x...
```

### Cách dùng số đó trong menu

```c
// GNames
uint8_t **blocks = (uint8_t **)(base + GNAMES_BLOCKS_RVA);
uint8_t  *entry  = blocks[id >> 16] + (id & 0xFFFF);   // id = ComparisonIndex
uint16_t  hdr    = *(uint16_t *)entry;                  // bIsWide:1, Len:15
const char *name = (const char *)(entry + 2);           // wide ? UTF-16 : ANSI

// GObjects
typedef struct { void **Objects; int32_t Max, Num, MaxChunks, NumChunks; } TUObjectArray;
// item: Objects[i>>16] + (i & 0xFFFF) * itemSize(0x10 hoac 0x18) -> UObject*
// class: *(void**)(obj+0x10) -> *(int32*)(uclass+0x18) -> FName id
```

---

## 6. Nếu log ghi `GNames NOT FOUND`

Đừng sửa loạn. Gửi mình đúng 3 thứ:

1. `zkprobe.txt` (toàn bộ)
2. Bản build đang chơi: `com.vng.pubgmobile` version + số build (Settings → About trong game)
3. Máy/chip + jb gì (Dopamine rootless? palera1n?)

Trong 90% trường hợp NOT FOUND là do: probe chạy quá sớm (chưa vào lobby), hoặc app chạy
**arm64 slice** mà dylib chỉ có **arm64e** (hoặc ngược lại) → lúc đó `NSLog` cũng không có dòng `[ZK]`
nào, tức là dylib chưa load chứ không phải dò sai.

---

## 7. Bước sau (chưa làm, cần log trước)

Có `GNames` + `GObjects` rồi thì mới đi được tiếp, và mấy cái này **phải verify bằng log**, không
đoán được:

- `PlayerController` → `PlayerCameraManager` → `CameraCachePrivate` (cho aim/ESP)
- `ControlRotation` (ghi rotation) — cần biết struct layout của `APlayerController` bản 4.6.0
- `RootComponent` → `RelativeLocation` / `ComponentToWorld` (world-to-screen)
- `Health` / `TeamID` trên `BP_PlayerCharacter_C` — thuộc tính của class蓝图, phải đọc qua property
  walk hoặc dump SDK

Có 2 đường lấy: (a) property walk runtime trên `UClass` (nếu build còn reflection), hoặc
(b) decrypt IPA 4.6.0 rồi IDA. Cả hai đều cần log ở bước này làm mốc.

---

## 8. Lưu ý thật

- Không có chuyện "undetected". ACE trên iOS quét danh sách image, dấu vết `/var/jb`, `sysctl(P_TRACED)`,
  port Frida `27042` / `gum-js-loop`, và quan trọng nhất là **hành vi** (tracking quá mượt, headshot
  rate, cross-map lock). Sống lâu = dylib tĩnh build từ source + hook bằng Dobby + aim có smoothing/FOV
  nhỏ/random delay, **không dùng Frida để chạy menu**.
- Đây là tool chẩn đoán (chỉ đọc + ghi log), chưa ghi gì vào memory game.
