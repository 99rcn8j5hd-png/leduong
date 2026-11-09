# helper_interface v2 — mock 4 lớp của bạn + PUBG provider cắm vào

Bản gốc bạn gửi (`main.cpp` + `sample.csv`) là **tầng 1–2**: một `ISnapshotProvider`
duy nhất cho hai nguồn dữ liệu khác nhau, `Reader` không biết bên dưới là gì.
Mình giữ nguyên hợp đồng đó và viết phần của mình: **provider thật đọc bộ nhớ PUBG**,
cùng một interface, thay nhau được mà không phải sửa `Reader`.

```
helper_interface_v2/
├── iface/
│   ├── snapshot.hpp          # hợp đồng dùng chung (append-only so với bản bạn gửi)
│   └── util.hpp              # now_ms()
├── src/
│   ├── main.cpp              # driver của bạn (đã vá 5 lỗi, xem FIXES_main.diff)
│   ├── mock_provider.hpp     # MockProvider — logic giữ nguyên
│   ├── csv_provider.hpp      # CsvProvider  — logic giữ nguyên, sửa 3 điểm nhỏ
│   └── pubg/
│       ├── pubg_provider.hpp/.cpp   # ← phần mình viết: provider thật
│       ├── mach_reader.hpp/.cpp     # read_mem + tìm exec base (điểm cắm kfd)
│       └── offsets.hpp              # đọc/ghi offsets.json, không hardcode
├── tools/dump_offsets.cpp    # dò offset bằng kiểm chứng, sinh offsets.json
├── build/Makefile            # host + ios + dump + syntax-apple
├── build/entitlements.plist  # quyền cho task_for_pid
├── offsets_pubg460.template.json
├── FIXES_main.diff           # diff giữa main.cpp gốc của bạn và bản đã vá
└── sample.csv
```

---

## 1. Năm lỗi trong `main.cpp` gốc (đã vá)

| # | Chỗ | Vấn đề | Hậu quả thật |
|---|-----|--------|--------------|
| 1 | `std::min(backoff*2, 2000)` | thiếu `#include <algorithm>` | build được hay không tuỳ libc; trên libc++ một số bản là **lỗi biên dịch** |
| 2 | `std::stoi(argv[...])` | ném `invalid_argument` không bắt | gõ sai tham số → **terminate**, không in usage |
| 3 | `CsvProvider::readSnapshot` gặp dòng generation khác → `return false` | giết **cả frame** vì 1 dòng lệch | stream thật (roster đổi giữa frame) sẽ liên tục ngắt kết nối |
| 4 | `Reader::sample` khi `generation` đổi | chỉ cache đúng những id **có mặt trong frame đó** | id chưa xuất hiện → bị `continue` **vĩnh viễn** cho tới lần đổi gen sau |
| 5 | Không xuất tên | `names_` được xây nhưng JSON không in `name` | tầng render không bao giờ thấy tên, cache vô nghĩa |

Ngoài ra: `sample()` trả `false` khi provider chết nhưng `main` chỉ `disconnect()`
mà không reset `backoff` — đã xử lý; và `frames` đếm vòng lặp chứ không đếm frame
phát ra (giữ nguyên hành vi, chỉ ghi chú).

Không đổi: tên class, chữ ký hàm, thứ tự 4 field đầu của `Player`.
`Snapshot` chỉ **thêm** field (`self`, `stamp_ms`), `Player` thêm `z/team/health/...`
ở cuối → `MockProvider`/`CsvProvider` của bạn biên dịch lại không phải sửa dòng nào.

## 2. Build

Tất cả target nằm trong `build/Makefile`, chạy được từ đâu cũng được
(nó tự tính đường dẫn gốc). Artifact ra `build/out/`.

```sh
# 1) kiểm tra biên dịch host (mock/csv chạy được thật)
make -f build/Makefile check
./build/out/helper-host mock 12 3
./build/out/helper-host csv sample.csv 12 3

# 2) kiểm tra nhánh Apple (không cần SDK — dùng header giả trong test/fakeapple)
make -f build/Makefile syntax-apple

# 3) build cho iPhone (cần macOS + Xcode; máy Linux không build được)
make -f build/Makefile ios
make -f build/Makefile sign      # = ldid -Sbuild/entitlements.plist

# hoặc chỉ build tool dò offset
make -f build/Makefile dump
ldid -Sbuild/entitlements.plist build/out/dump_offsets
```

Trên **Dopamine roothide**: `ldid` xong vẫn phải `roothidepatch` binary trước khi
chép vào máy, nếu không nó `Killed: 9` và bạn sẽ tưởng nhầm là "task port bị chặn".

## 2.5 Ba điều kiện để dump ra số thật (đọc trước, đỡ mất một vòng)

1. **Chạy bằng root** (`ssh root@<ip>`, pass mặc định `alpine`) — task_for_pid vào
   process khác bắt buộc root + entitlement `task_for_pid-allow` (đã có trong plist).
2. **Phải đang ở trong thế giới game**, không phải màn login / lobby chờ:
   `GWorld` chỉ khác 0 khi `UWorld` đã được tạo, và ở lobby số actor quá ít nên
   bước kiểm chứng `Actors` (cần ≥50% phần tử resolve được) sẽ FAIL oan.
   Vào **Training / một trận đang chạy** rồi hãy dump.
3. **Giữ app ở foreground** — iOS suspend process nền, đọc ra vùng nhớ đóng băng
   hoặc fail giữa đường.

## 3. Dò offset (làm trước mọi thứ khác)

```sh
# lấy pid game (bản VNG: exec name thật có thể không phải ShadowTrackerExtra)
ps -A | grep -i pubg
./dump_offsets --pid <pid> --out offsets.json 2>&1 | tee dump.log
# nếu bước base fail:
./dump_offsets --pid <pid> --image <tên exec> --out offsets.json 2>&1 | tee dump.log
```

Base đổi mỗi lần mở app, nhưng offset là **tương đối** → dump một lần, dùng tới
lần update kế tiếp.

## 3. Dò offset (làm trước mọi thứ khác)

```sh
# lấy pid game
pgrep -l ShadowTrackerExtra   # hoặc tên exec thật của bản VNG
./dump_offsets --pid <pid> --out offsets.json
```

Tool **không đoán**: mỗi bước chỉ nhận khi vượt kiểm chứng ngữ nghĩa.

- `GNames`: quét `__DATA*`, thử 4 layout (TNameEntryArray 4.18 và FNamePool 4.23+)
  × 2 mức deref; chỉ nhận khi ≥40/64 entry đầu đọc ra chuỗi ASCII.
- `GObjects`: thử `chunk_shift ∈ {13,14}` × `item_stride ∈ {0x10,0x18,0x20}` ×
  `object ∈ {0,8}` × `(class,name) ∈ {0x10/0x18, 0x18/0x20, 0x20/0x28}`;
  chỉ nhận khi ≥50/64 object có **name và class name đều resolve được**.
- `GWorld`: tìm object có class name `World`, rồi quét `__DATA*` tìm biến global
  trỏ đúng object đó.
- `PersistentLevel`: quét `UWorld+0..0x800`, nhận offset mà giá trị đọc ra có
  **object name == "PersistentLevel"**.
- `Actors`: quét `ULevel+0..0x1000` tìm `TArray` có `Data` hợp lệ và ≥50% phần tử
  đầu là con trỏ tới object có name resolve được.
- `RootComponent` + `ComponentToWorld`: quét `AActor+0x80..0x400` tìm component,
  rồi quét `component+0..0x600` tìm `FTransform` có **quaternion chuẩn hoá ≈1**
  và translation nằm trong biên hợp lý.

Sai là **FAIL + in lý do**, không ghi số rác vào json. `pubg_provider` cũng
fail-early: world chain sai → dừng ngay, không đọc tiếp suốt trận.

## 4. Chạy

```sh
./helper mock 12 3
./helper csv sample.csv 12 3
./helper pubg 600 1 --pid <pid> --offsets offsets.json
```

Ba mode, **cùng một `Reader`**. Đổi provider = đổi argv, không đổi code.

## 5. Chỗ cắm vào tầng chống phát hiện của bạn

`pubg_provider` chỉ dùng `mach_vm_read_overwrite` từ một binary riêng:
không inject, không thread trong process game, không image lạ.

- Trước khi lấy task port, `connect()` gọi `attach_trace_selftest()` →
  in `P_TRACED`. Nếu bị set, nó **không** đi tiếp bằng task port mà in cảnh báo
  và trả `false` — chỗ này bạn thay bằng `KernelReader`:

```cpp
// pubg_provider.cpp, connect()
// ctx_->rd = std::make_unique<KernelReader>(kfd_handle, phys_read_fn);
// ctx_->rd->setImageBase(base);
```
Logic `read_world_chain` / `read_player` / `name_of` không phải sửa một dòng,
vì tất cả đọc qua `IMemReader::read`.

- Nhịp đọc: 30Hz, chia **4 lượt/vòng** (`kSlots`), mỗi lượt đọc 1/4 số actor →
  mật độ truy vấn thấp hơn 60Hz toàn bộ mà cảm giác vẫn mượt. World chain chỉ
  refresh 2Hz.
- `readSnapshot` trả bản copy dưới mutex; worker thread ghi `latest_`, nên
  tầng render không bao giờ đọc phải snapshot nửa vời.

## 6. Còn thiếu (nói thẳng, không giấu)

1. **`ps_team / ps_health / ps_name / pawn_player_state / pawn_mesh / mesh_bones /
   pc_camera_mgr / cam_pov_loc` chưa được dò tự động.** `dump_offsets` hiện chỉ
   chốt được: base, gnames, gobjects, uobject layout, gworld, persist_level,
   level_actors, actor_root, comp_xform. Phần còn lại cần một tool `dump_player`
   (cùng phương pháp: leo object → kiểm chứng ngữ nghĩa). Hiện tại provider vẫn
   chạy, ra **vị trí + dist**, còn tên/team/HP để trống (`team=-1`, `health=0`).
2. **`bone_head` phải lấy từ skeleton dump** của đúng `USkeletalMesh` đang dùng —
   index đầu/ngực/cổ khác nhau giữa nam/nữ. Chưa có dump bone trong bản này.
3. **`KernelReader` mới là khung**, chưa nối kfd/physrw thật (cần API của bản kfd
   bạn đang dùng — ptrauth, t1sz, bảng trang khác nhau giữa các bản).
4. Code Apple **chưa chạy thật** ở đây — mình chỉ mới kiểm bằng `-fsyntax-only`
   trên header giả (`make syntax-apple`). Phải build + chạy trên máy bạn để chốt
   offset; đừng coi số trong template là số thật.

## 7. Tóm tắt tiếng Trung

你给的 4 层接口我一行没改（只在末尾追加字段，老 provider 不用动）。我补的是
`pubg_provider`：所有 offset 都从 `offsets.json` 读，不写死；dump 工具靠语义校验
（FName 能读成字符串、ULevel 的名字必须叫 PersistentLevel、四元数必须归一化）来
确认 offset，不靠猜。`read_mem` 抽成 `IMemReader`，所以 task port 被 P_TRACED
挡掉时，换 kfd 后端不用动上面的逻辑。
没做完的写在上面第 6 节：ps/mesh 那批 offset 还没自动 dump、bone index 要从
skeleton dump 取、kfd 只是接口桩、Apple 代码只过了语法检查没真机跑。
