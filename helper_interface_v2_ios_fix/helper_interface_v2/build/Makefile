# helper_interface_v2 — build
#
#   make check         biên dịch toàn bộ trên máy đang đứng (mock/csv chạy được)
#   make dump          out/dump_offsets   (chỉ build được trên macOS/iOS)
#   make ios           out/helper + out/dump_offsets arm64 cho iPhone (cần Xcode + SDK)
#   make sign          ký bằng ldid với entitlements.plist
#   make syntax-apple  kiểm tra nhánh Apple bằng header giả (chạy được cả trên Linux)
#   make clean
#
# Chạy được cả từ trong build/ (make dump) lẫn từ root (make -f build/Makefile dump).
#
CXX      ?= clang++
CXXFLAGS ?= -std=c++17 -O2 -g -Wall -Wextra -Wno-unused-parameter -pthread

MK       := $(patsubst %/,%,$(dir $(abspath $(lastword $(MAKEFILE_LIST)))))
ROOT     := $(MK)/..
OUT      := $(MK)/out
INC      := -I$(ROOT) -I$(ROOT)/iface -I$(ROOT)/src

IOS_SDK  := $(shell xcrun --sdk iphoneos --show-sdk-path 2>/dev/null)
IOS_MIN  := -miphoneos-version-min=15.0
APPLE    := $(shell uname -s)

$(shell mkdir -p $(OUT))

# ---- host: mock + csv thật, pubg là stub (in ra "chỉ chạy trên iOS") ----
check:
	$(CXX) $(CXXFLAGS) $(INC) -o $(OUT)/helper-host \
		$(ROOT)/src/main.cpp $(ROOT)/src/pubg/pubg_provider.cpp $(ROOT)/src/pubg/mach_reader.cpp
	@echo "[ok] $(OUT)/helper-host  (mock / csv chạy được)"

# ---- dump_offsets: cần mach_vm + libproc ----
dump:
	@if [ "$(APPLE)" != "Darwin" ] && [ -z "$(IOS_SDK)" ]; then \
		echo "[!] dump_offsets cần macOS hoặc iOS SDK. Máy này chỉ build được helper-host."; \
	else \
		$(CXX) $(CXXFLAGS) $(INC) -o $(OUT)/dump_offsets \
			$(ROOT)/tools/dump_offsets.cpp $(ROOT)/src/pubg/mach_reader.cpp $(ROOT)/src/pubg/pubg_provider.cpp; \
		echo "[ok] $(OUT)/dump_offsets"; \
	fi

# ---- binary cho iPhone (arm64, iOS 15+) ----
ios:
	@if [ -z "$(IOS_SDK)" ]; then echo "[!] không thấy iphoneos SDK — cài Xcode / Command Line Tools"; exit 1; fi
	$(CXX) $(CXXFLAGS) $(INC) -arch arm64 -isysroot $(IOS_SDK) $(IOS_MIN) \
		-fembed-bitcode-marker -o $(OUT)/helper \
		$(ROOT)/src/main.cpp $(ROOT)/src/pubg/pubg_provider.cpp $(ROOT)/src/pubg/mach_reader.cpp
	$(CXX) $(CXXFLAGS) $(INC) -arch arm64 -isysroot $(IOS_SDK) $(IOS_MIN) \
		-o $(OUT)/dump_offsets \
		$(ROOT)/tools/dump_offsets.cpp $(ROOT)/src/pubg/mach_reader.cpp $(ROOT)/src/pubg/pubg_provider.cpp
	@echo "[ok] $(OUT)/helper + $(OUT)/dump_offsets (arm64)"
	@echo "     tiếp: make sign   — hoặc: ldid -S$(MK)/entitlements.plist $(OUT)/dump_offsets"

sign:
	-ldid -S$(MK)/entitlements.plist $(OUT)/helper
	ldid -S$(MK)/entitlements.plist $(OUT)/dump_offsets
	@echo "[ok] đã ký. roothide: roothidepatch binary TRƯỚC khi chép sang máy."

# ---- chỉ kiểm tra cú pháp nhánh Apple, không cần SDK ----
syntax-apple:
	@echo "Deprecated: fake headers do not verify iOS compatibility. Run the iOS SDK build in GitHub Actions."
	@exit 1

clean:
	rm -rf $(OUT)

.PHONY: check dump ios sign syntax-apple clean
