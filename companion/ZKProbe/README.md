# ZKProbe — reviewed experimental diagnostic

Được sửa từ pubg-gnames46.zip người dùng cung cấp. Đây là công cụ chẩn đoán runtime độc lập, chưa phải plugin ESP/Aimbot, chưa xác nhận tìm được bảng tên của PUBG VN 4.6.0. Không xác nhận tuyên bố hỗ trợ mọi UE4.18+ trong README gốc. README_original.md được giữ để đối chiếu, không phải hướng dẫn ưu tiên.

## Đã sửa

- Parser C++ dùng kích thước buffer thực thay vì mặc định 64 KB; có bài test buffer 8 KB và entry bị cắt.
- Dylib quét trên background queue; khóa ngăn chạy chồng; đóng log; làm mới mapping giữa các lần probe; giải phóng Mach port trả về khi liệt kê region.
- Nhận diện main executable bằng MH_EXECUTE; kiểm tra từng mapped segment và bỏ PAGEZERO. Không chọn image chỉ vì đường dẫn chứa chữ PUBG.
- Bỏ poolAddr=Blocks-0x10. Không báo một địa chỉ heap là RVA hợp lệ của executable.
- Buffer quét giảm xuống 256 KB, mỗi lượt có ngân sách 2 GB dữ liệu quét/90 giây kiểm tra tại các bước quét. Ngân sách không phải hard timeout cho mọi phép xử lý. Hết ngân sách được báo INCOMPLETE.
- Frida không tự quét sau khi nạp; gọi zkProbe() khi muốn kiểm tra. Chia nhỏ việc đọc vùng object, giới hạn mẫu tên, sửa chuyển pointer sang byte tránh mất độ chính xác số JavaScript.
- Dylib đầu ra tên ZKProbe.dylib, tránh ghi đè PUBGKit.dylib. Build tách C++ và Objective-C++ để ARC chỉ áp dụng đúng ngôn ngữ.

## Giới hạn còn lại — cần đọc

Layout vẫn là giả thuyết: headerShift=1, offset ID tính theo byte, căn entry 2 byte, block 64 KB, class/name fields và object array layout cố định. Kết quả chỉ là CANDIDATE. Chưa hỗ trợ tự nhận dạng mọi layout và chưa xác nhận trên thiết bị.

Parser chỉ chấp nhận ký tự ASCII ngay cả với tên wide; tên khác bị loại. Giới hạn 512 block là trần quét, chưa dùng CurrentBlock/CurrentByteCursor của allocator. Địa chỉ biến động khi game đang chạy; phép quét không phải snapshot nhất quán. Giới hạn thời gian/bộ nhớ có thể làm bỏ sót; không suy ra không tồn tại bảng tên khi không tìm thấy. Tìm nhiều tên hợp lệ vẫn cần đối chiếu quan hệ đối tượng và ID trước khi dùng kết quả.

Frida chọn module chính theo tên như bản gốc và ưu tiên phạm vi 256 MB theo heuristic, không phải bằng chứng module ownership; không dùng RVA của kết quả đó trực tiếp. Script chỉ dùng chẩn đoán. Không coi ESign thông thường tương đương quyền chạy mã của TrollStore/jailbreak.

## Kiểm tra và build

Windows có Node: node tests/parser_test.js

Mac: clang++ -std=c++17 -fsanitize=address,undefined -g tests/parser_test.cpp -o /tmp/zkprobe-parser-test && /tmp/zkprobe-parser-test

Sau đó bash build.sh (cần Xcode và ldid). Kết quả artifacts/ZKProbe.dylib và SHA256.txt. Workflow build-zkprobe.yml được cung cấp để thêm vào .github/workflows/ của repository hiện có, với các nguồn ở companion/ZKProbe/.

Chưa có bản dylib đã build tại máy Windows này. Workflow chưa chạy. Không nạp cùng lúc các probe cũ vì nhiều lần quét có thể tốn CPU và gây nhiễu mẫu.

## Kết quả

Dylib tự probe một lần sau khoảng 12 giây, ghi zkprobe.txt ở đường dẫn được in trong log (ưu tiên /var/mobile/Documents, fallback Documents của app). Đây không phải probe.log của PUBGKit. Không có menu riêng. Không tự chép kết quả vào offsets.plist hoặc tích hợp ESP/tự ngắm.
