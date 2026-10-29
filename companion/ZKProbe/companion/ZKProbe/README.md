# ZKProbe targeted-v1

Bản kiểm chứng ứng viên, chưa phải ESP/Aimbot và chưa xác nhận tìm được GNames lúc chạy.

## Bằng chứng mới

Executable có UUID bef90c8f5cb63a9c81c1480e7fafaac4 có đường tra tên tại địa chỉ file 0x104fd14c4: gọi accessor 0x104fd0b5c, chia ID bằng SDIV cho 0x4000 rồi đọc hai tầng con trỏ. Hàm so sánh tên lân cận dùng flags entry+8 và chuỗi entry+12. Accessor dùng wrapper tại địa chỉ file 0x10a446b20; wrapper này KHÔNG phải con trỏ bảng tên có thể dereference một lần. Các địa chỉ này là kết quả phân tích tĩnh, chưa phải xác nhận thiết bị.

## Bản này thực hiện

Sau khoảng 12 giây, chạy một lần trên background queue. Kiểm tra đúng UUID; khác UUID thì dừng trước khi đọc ứng viên. Chỉ đọc header wrapper, theo chuỗi tối đa 16 tầng rồi thử entry đầu trong chunk0. Không gọi accessor/hàm game, không khởi tạo wrapper, không quét memory, không thay đổi game hoặc xác thực. Đọc tối đa 4096 byte yêu cầu/128 lần/2 giây tại các điểm đọc. Log gồm tên mẫu hoặc lý do dừng, lưu Documents/zkprobe-targeted.txt của game. Không có menu riêng.

Tag wrapper và model ASCII được kiểm tra chặt; giá trị bất thường hoặc tên wide sẽ dừng. Không tìm được không chứng minh bảng tên không tồn tại. Đọc được 16 tên cũng mới là ứng viên, chưa đủ để tích hợp ESP/Aimbot.

## Cài nguồn để build

ZIP giữ đúng đường dẫn companion/ZKProbe và .github/workflows/build-zkprobe.yml. Thay file cùng đường dẫn trong repo. Workflow: Build ZKProbe Targeted. Artifact: ZKProbe-targeted-candidate, có ZKProbe.dylib.

Giữ bản ZKProbe cũ tắt. Nếu thử bản mới, chỉ bật một bản. Gửi zkprobe-targeted.txt sau một lượt; không cần bật bản quét cũ. Nếu tiếp tục văng, tắt bản mới và dừng thử lặp lại.

## Kiểm tra

Chưa có C++ compiler/iOS SDK trên máy Windows này; chưa chạy C++ hoặc build dylib tại đây. CI chạy tests/targeted_test.cpp với AddressSanitizer/UBSan trước khi build. Test mô phỏng bảng hợp lệ, wrapper chưa init, tag không hợp lệ, vòng con trỏ và tên đầu sai. Không thay thế kiểm chứng trên thiết bị.
