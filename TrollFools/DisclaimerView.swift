import SwiftUI

struct DisclaimerView: View {
    @Binding var isDisclaimerHidden: Bool
    var body: some View {
        NavigationView {
            VStack(alignment: .leading, spacing: 20) {
                Text("leduong").font(.largeTitle).bold()
                Text("Chọn ứng dụng và quản lý plugin trên thiết bị của bạn.")
                Text("Nhập tệp .dylib, .deb hoặc .zip từ ứng dụng Tệp. Dùng Inject để nạp và Eject để gỡ plugin.")
                Text("Không cần tài khoản hay mã kích hoạt.")
                Button("Bắt đầu") { isDisclaimerHidden = true }
                    .font(.headline).padding(.vertical)
                Spacer()
            }
            .padding()
            .navigationBarTitleDisplayMode(.inline)
        }
    }
}
