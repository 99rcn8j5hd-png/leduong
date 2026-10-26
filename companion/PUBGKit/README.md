# PUBGKit 0.2.0 — sửa trực tiếp từ pubgkit-ios-46.zip

Giữ luồng đọc dữ liệu thật trong tiến trình game và mã ESP/tự ngắm của dự án gốc. Đây là bản sửa nguồn, chưa phải bản phát hành đã kiểm chứng. Không có .deb đã build trong gói này.

## Đã sửa

- Makefile đặt common.mk đúng vị trí, cấu hình rootless mặc định, tách ARC/C++17 theo ngôn ngữ và thêm quy tắc stage offsets.plist.
- Thêm header Mach VM cần thiết; kiểm tra con trỏ đích và phép cộng địa chỉ tràn trong các hàm bộ nhớ; giới hạn ghi trong một region và thử khôi phục quyền trang ban đầu.
- Khóa trạng thái core khi đọc/dò lại, chụp cấu hình với mutex, thay cờ volatile của worker bằng atomic; khóa log và khởi tạo đường dẫn một lần.
- Dò lại xóa trạng thái runtime cũ; kiểm tra số lượng actor hiện tại, chỉ báo sẵn sàng khi có world/controller/camera.
- Menu có cuộn để truy cập toàn bộ nội dung, công tắc khớp giá trị mặc định; chạm ngoài panel/bong bóng được trả về game. Overlay không chiếm key window; ESP xóa nội dung cũ khi vẽ lại.
- Sửa hướng trục phải trong worldToScreen; chặn kích thước/góc không hữu hạn và vòng lặp chuẩn hóa yaw vô hạn. Thêm vòng FOV khi bật tự ngắm.
- Stealth.mm được giữ trong reference/Stealth.mm.disabled để truy vết, không được biên dịch hoặc gọi. Không thêm cơ chế tránh phát hiện.

## Build Dopamine Rootless

Cần macOS + Xcode + Theos, iOS SDK, ldid và công cụ đóng gói theo tài liệu chính thức https://theos.dev/docs/installation-macos . Cấu hình build theo https://theos.dev/docs/variables và https://theos.dev/docs/rootless . Môi trường Windows hiện tại không có compiler iOS/Theos.

```sh
export THEOS="$HOME/theos"  # sửa theo nơi cài thực tế
python3 scripts/verify_source.py
c++ -std=c++17 tests/math_test.cpp -o /tmp/pubgkit-math-test
/tmp/pubgkit-math-test
make clean
make package FINALPACKAGE=1 THEOS_PACKAGE_SCHEME=rootless
```

Gói được tạo trong packages/. Dùng dpkg-deb -f FILE.deb Architecture và dpkg-deb -c FILE.deb để kiểm tra iphoneos-arm64, dylib/filter ở /var/jb/Library/MobileSubstrate/DynamicLibraries và cấu hình mặc định ở /var/jb/Library/Application Support/PUBGKit/offsets.plist. Dùng lipo -info với dylib thực tế trong .theos/obj để kiểm tra arm64/arm64e. Không thêm tiền tố /var/jb lần nữa.

Chuyển .deb sang iPhone, cài bằng Sileo khi dependency được đáp ứng, rồi đóng/mở lại game. Dự án không còn tự kill -9 ứng dụng sau cài. Dependency mobilesubstrate là giao diện injection cần bootstrap đáp ứng.

## Cấu hình và log

Mặc định ESP/tự ngắm tắt, controllerControlRotation=0. Giữ mặc định này khi kiểm tra phần đọc dữ liệu. Không bổ sung offset cụ thể cho PUBG 4.6.0.

Ứng dụng ghi log vào Documents/PUBGKit trong container của chính ứng dụng (fallback thư mục tạm nếu không tạo được). Nút PROBE hiển thị đường dẫn. Cấu hình người dùng offsets.plist tại cùng thư mục được ưu tiên; nếu không có thì dùng tài nguyên mặc định đã đóng gói. Đọc cấu hình lúc boot; thay đổi cần khởi động lại ứng dụng. Dò lại chỉ xóa trạng thái runtime, không nạp lại file.

GNames/GObjects hiện được code sử dụng như địa chỉ tuyệt đối trong phiên chạy, mặc dù comment gốc gọi là RVA. Không tái sử dụng địa chỉ tuyệt đối sau khi khởi động lại vì ASLR. Các heuristic discovery chưa được xác minh với layout đối tượng của PUBG hiện tại; tên class hiển thị cũng chưa được xác nhận là đúng tên lớp UE.

## Kiểm thử và giới hạn còn lại

Đã chạy kiểm tra tĩnh bằng Python: cấu trúc, plist tài nguyên, danh sách nguồn Makefile, filter và cờ build. Bài kiểm thử C++ dùng trực tiếp UE.hpp được cung cấp nhưng chưa chạy vì máy hiện tại thiếu compiler C++. Chưa biên dịch Objective-C/Logos, link, tạo .deb hoặc thử trên thiết bị.

Trên thiết bị cần kiểm tra: mở menu và cuộn tới nút cuối; chạm xuyên ngoài menu; bật/tắt ESP không để lại hình cũ; probe và dò lại không treo UI; đọc actor đúng sau đổi trận; xoay màn hình, khóa/mở khóa và chi phí CPU/pin. Cửa sổ chưa có xử lý scene/rotation chuyên biệt nên phải kiểm tra landscape thực tế.

Chưa thể bảo đảm chạy đúng game: object array có thể khác giả định chunked; cache world/camera có thể lỗi thời khi đổi map; khôi phục quyền trang và tính đúng của memWrite chưa được kiểm thử; aimKeyAlways chưa được thực thi và công thức dự đoán còn giả định đơn vị tốc độ. Không có kết quả kiểm thử để khẳng định tương thích phiên bản 4.6.0 hoặc độ ổn định. Đừng coi việc menu xuất hiện là bằng chứng đọc đúng dữ liệu.

Gỡ bằng Sileo. Nếu cần sửa tiếp theo lỗi thực tế, cung cấp log build đầu tiên hoặc probe.txt cùng model máy, iOS và phiên bản game. README gốc nằm trong reference/ và chỉ phục vụ đối chiếu.
