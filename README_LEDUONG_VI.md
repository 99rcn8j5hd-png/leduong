# leduong — loader cục bộ

Đây là mã nguồn loader dựa trên TrollFools (MIT), tên hiển thị leduong, bundle vn.leduong.loader. Giữ danh sách ứng dụng, nhập plugin .dylib/.deb/.zip và Inject/Eject của upstream. Có dịch các mục giao diện chính sang tiếng Việt trong bản địa hóa tiếng Anh mặc định; các bản địa hóa khác vẫn của upstream. Giữ thông tin tác giả và LICENSE.

Không thêm đăng nhập, mã kích hoạt, khóa UDID/serial, license server, APIAuthManager hoặc mã hóa cấu hình để quản lý người dùng. Tắt kiểm tra cập nhật tự động và hiển thị quảng cáo. Các liên kết tác giả/mã nguồn vẫn có thể mở khi người dùng nhấn. Không sửa/bẻ cơ chế xác thực của binary X2NIOS: dự án này bắt đầu từ mã nguồn công khai. Thư viện crypto và công cụ ký của upstream vẫn cần cho xử lý binary, không phải cơ chế khóa người dùng.

## Build

Chưa build .tipa trong môi trường Windows này; không dùng leduong.tipa đổi tên ở lượt trước để thay thế bản nguồn mới này.

Cần máy macOS, Xcode theo workflow upstream (15.4), Theos cùng SDK và brew dependencies. Tham khảo .github/workflows/compile.yml để thiết lập chính xác. Lần đầu resolve Swift Packages cần mạng; Inject/Eject không được gắn với tài khoản hoặc API riêng.

```sh
export THEOS="$HOME/theos"
make clean
make package FINALPACKAGE=1 THEOS_PACKAGE_SCHEME=rootless
```

Artifact khi thành công: packages/leduong_1.0.0-1.tipa và gói .deb. Hoặc đưa mã nguồn lên repository do bạn quản lý rồi chạy workflow Compile bằng workflow_dispatch; artifact leduong-packages. Chưa chạy workflow trong phiên này. Không tự publish release.

.tipa dành cho TrollStore trên thiết bị được hỗ trợ; jailbreak Dopamine không tự chứng minh TrollStore đã cài. Chưa xác nhận trên thiết bị cụ thể. Tên thư mục .app và executable bên trong giữ TrollFools để không phá cấu trúc build.

## PUBGKit

companion/PUBGKit/ là bản nguồn plugin đã sửa, vẫn độc lập với loader. Build theo README của plugin để tạo dylib/.deb. Chưa có binary plugin kèm sẵn. Sau khi có loader và plugin được build, nhập tệp cục bộ từ Files. Việc loader nạp được plugin không chứng minh offset hoặc chức năng game đúng.

Lưu ý: inject từ .deb không thực hiện toàn bộ cài đặt tài nguyên/script giống Sileo. Đối với PUBGKit, kiểm tra đường dẫn offsets.plist trong README riêng; không cho rằng tài nguyên được chép tự động bởi loader. Tránh cài cùng plugin bằng cả Sileo và loader để không nạp hai lần.

## Xác minh

Đã kiểm tra tĩnh metadata, interface lớp cập nhật, nguồn gốc/license, nguồn Inject/Eject còn nguyên và không có APIAuthManager trong mã Swift loader. Chưa chạy xcodebuild, đóng gói thực tế hoặc kiểm thử trên iPhone. Chưa xác minh đầy đủ UI tiếng Việt, tính tương thích scene/thiết bị và inject runtime. Sau khi build, kiểm tra danh sách ứng dụng, nhập plugin, Inject/Eject, mở lại ứng dụng và thử khi tắt mạng để xác nhận không có bước kích hoạt.
